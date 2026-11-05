#pragma once
#ifdef LMS_HAS_MYSQL
#include "lms/errors.h"
#include <mysql.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lms {
class Stmt {
    MYSQL_STMT* stmt_=nullptr;
    struct Parameters {
        std::vector<MYSQL_BIND> binds;
        std::vector<std::string> strings;
        std::vector<unsigned long> lengths;
        std::vector<int> integers;
        std::vector<double> doubles;
        explicit Parameters(std::size_t size):binds(size),strings(size),lengths(size),integers(size),doubles(size) {}
    };
    using NullFlag=std::remove_pointer_t<decltype(MYSQL_BIND{}.is_null)>;
    struct Results {
        std::vector<MYSQL_BIND> binds;
        std::vector<std::string> strings;
        std::vector<unsigned long> lengths;
        std::unique_ptr<NullFlag[]> nulls;
        explicit Results(std::size_t size):binds(size),strings(size),lengths(size),nulls(new NullFlag[size]{}) {}
    };
    // Fixed-size buffers live on the heap: binding another parameter or moving Stmt
    // cannot invalidate pointers already registered with the MySQL C API.
    std::unique_ptr<Parameters> params_;
    std::unique_ptr<Results> results_;
    MYSQL_BIND& parameter(int index) {
        if (!params_ || index<0 || static_cast<std::size_t>(index)>=params_->binds.size())
            throw std::invalid_argument("Invalid SQL parameter index");
        auto& bind=params_->binds[index]; bind=MYSQL_BIND{};return bind;
    }
public:
    Stmt()=default;
    ~Stmt(){if(stmt_)mysql_stmt_close(stmt_);}
    Stmt(const Stmt&)=delete;
    Stmt& operator=(const Stmt&)=delete;
    Stmt(Stmt&& other) noexcept:stmt_(std::exchange(other.stmt_,nullptr)),params_(std::move(other.params_)),results_(std::move(other.results_)) {}
    Stmt& operator=(Stmt&& other) noexcept {
        if(this!=&other){if(stmt_)mysql_stmt_close(stmt_);stmt_=std::exchange(other.stmt_,nullptr);params_=std::move(other.params_);results_=std::move(other.results_);}return *this;
    }
    bool init(MYSQL* connection,const char* sql) {
        stmt_=mysql_stmt_init(connection);
        if(!stmt_ || mysql_stmt_prepare(stmt_,sql,static_cast<unsigned long>(std::strlen(sql)))!=0)
            throw DatabaseError("Cannot prepare database statement");
        params_=std::make_unique<Parameters>(mysql_stmt_param_count(stmt_));return true;
    }
    void bind_int(int index,int value){auto& b=parameter(index);params_->integers[index]=value;b.buffer_type=MYSQL_TYPE_LONG;b.buffer=&params_->integers[index];}
    void bind_double(int index,double value){auto& b=parameter(index);params_->doubles[index]=value;b.buffer_type=MYSQL_TYPE_DOUBLE;b.buffer=&params_->doubles[index];}
    void bind_string(int index,const std::string& value){auto& b=parameter(index);params_->strings[index]=value;params_->lengths[index]=static_cast<unsigned long>(value.size());b.buffer_type=MYSQL_TYPE_STRING;b.buffer=params_->strings[index].data();b.buffer_length=params_->lengths[index];b.length=&params_->lengths[index];}
    bool execute(){
        if(!params_->binds.empty() && mysql_stmt_bind_param(stmt_,params_->binds.data())!=0)throw DatabaseError("Cannot bind database parameters");
        if(mysql_stmt_execute(stmt_)!=0){if(mysql_stmt_errno(stmt_)==1062)return false;throw DatabaseError("Database statement failed");}
        return true;
    }
    long long affected_rows()const{return static_cast<long long>(mysql_stmt_affected_rows(stmt_));}
    long long last_insert_id()const{return static_cast<long long>(mysql_stmt_insert_id(stmt_));}
    bool store_result(){
        bool update_lengths=true;
        if(mysql_stmt_attr_set(stmt_,STMT_ATTR_UPDATE_MAX_LENGTH,&update_lengths)!=0 || mysql_stmt_store_result(stmt_)!=0)
            throw DatabaseError("Cannot read database result");
        return true;
    }
    bool fetch_row(std::vector<std::string>& output){
        if(!results_){
            auto* metadata=mysql_stmt_result_metadata(stmt_);
            if(!metadata)throw DatabaseError("Database result metadata missing");
            std::unique_ptr<MYSQL_RES,decltype(&mysql_free_result)> guard(metadata,mysql_free_result);
            const auto columns=mysql_num_fields(metadata);auto* fields=mysql_fetch_fields(metadata);
            results_=std::make_unique<Results>(columns);
            for(unsigned int i=0;i<columns;++i){
                if(fields[i].max_length>1024*1024)throw DatabaseError("Database field exceeds result limit");
                auto& buffer=results_->strings[i];buffer.resize(std::max<unsigned long>(32,fields[i].max_length+1));
                auto& bind=results_->binds[i];bind=MYSQL_BIND{};bind.buffer_type=MYSQL_TYPE_STRING;
                bind.buffer=buffer.data();bind.buffer_length=static_cast<unsigned long>(buffer.size());
                bind.length=&results_->lengths[i];bind.is_null=&results_->nulls[i];
            }
            if(mysql_stmt_bind_result(stmt_,results_->binds.data())!=0)throw DatabaseError("Cannot bind database results");
        }
        const int result=mysql_stmt_fetch(stmt_);
        if(result==MYSQL_NO_DATA)return false;
        if(result!=0)throw DatabaseError("Database result failed or was truncated");
        output.resize(results_->strings.size());
        for(std::size_t i=0;i<output.size();++i)
            output[i]=results_->nulls[i]?std::string{}:std::string(results_->strings[i].data(),results_->lengths[i]);
        return true;
    }
    MYSQL_STMT* get(){return stmt_;}
};

class [[nodiscard]] MysqlConn {
    MYSQL* connection_=nullptr;
public:
    MysqlConn()=default;
    ~MysqlConn(){if(connection_)mysql_close(connection_);}
    MysqlConn(const MysqlConn&)=delete;
    MysqlConn& operator=(const MysqlConn&)=delete;
    MysqlConn(MysqlConn&& other) noexcept:connection_(std::exchange(other.connection_,nullptr)){}
    MysqlConn& operator=(MysqlConn&& other) noexcept {if(this!=&other){if(connection_)mysql_close(connection_);connection_=std::exchange(other.connection_,nullptr);}return *this;}
    bool connect(const char* host,const char* user,const char* password,const char* database,int port=3306){
        if(connection_)mysql_close(connection_);connection_=mysql_init(nullptr);if(!connection_)return false;
        unsigned int timeout=3;
        mysql_options(connection_,MYSQL_OPT_CONNECT_TIMEOUT,&timeout);
        mysql_options(connection_,MYSQL_OPT_READ_TIMEOUT,&timeout);
        mysql_options(connection_,MYSQL_OPT_WRITE_TIMEOUT,&timeout);
        if(!mysql_real_connect(connection_,host,user,password,database,port,nullptr,CLIENT_FOUND_ROWS) || mysql_set_character_set(connection_,"utf8mb4")!=0){mysql_close(connection_);connection_=nullptr;return false;}
        return true;
    }
    MYSQL* get(){return connection_;}
    bool alive()const{return connection_ && mysql_ping(connection_)==0;}
    bool exec(const char* sql){if(mysql_query(connection_,sql)!=0)throw DatabaseError("Database command failed");return true;}
    Stmt prepare(const char* sql){Stmt statement;statement.init(connection_,sql);return statement;}
};

class Transaction {
    MysqlConn& connection_;bool committed_=false;
public:
    explicit Transaction(MysqlConn& connection):connection_(connection){connection_.exec("START TRANSACTION");}
    ~Transaction(){if(!committed_)mysql_rollback(connection_.get());}
    void commit(){if(mysql_commit(connection_.get())!=0)throw DatabaseError("Database commit failed");committed_=true;}
    Transaction(const Transaction&)=delete;
    Transaction& operator=(const Transaction&)=delete;
};

class ConnectionPool {
public:
    struct Config {std::string host="127.0.0.1",user="root",password,database="lms_db";int port=3306,pool_size=8;};
    static ConnectionPool& instance(){static ConnectionPool pool;return pool;}
    void init(const Config& config){
        std::lock_guard<std::mutex> lock(mutex_);
        if(initialized_)return;
        if(config.pool_size<1 || config.pool_size>128)throw std::invalid_argument("Invalid database pool size");
        config_=config;std::queue<MysqlConn> ready;
        for(int i=0;i<config.pool_size;++i){auto connection=create_connection();if(!connection)throw DatabaseError("Cannot initialize database pool");ready.push(std::move(*connection));}
        pool_=std::move(ready);initialized_=true;
    }
    class [[nodiscard]] Lease {
        ConnectionPool* pool_;MysqlConn connection_;
    public:
        Lease(ConnectionPool* pool,MysqlConn&& connection):pool_(pool),connection_(std::move(connection)){}
        ~Lease(){if(pool_)pool_->give_back(std::move(connection_));}
        Lease(Lease&& other) noexcept:pool_(std::exchange(other.pool_,nullptr)),connection_(std::move(other.connection_)){}
        Lease(const Lease&)=delete;
        MYSQL* get(){return connection_.get();}
        bool alive()const{return connection_.alive();}
        MysqlConn* operator->(){return &connection_;}
        MysqlConn& operator*(){return connection_;}
    };
    Lease acquire(){
        struct ThreadContext {
            ThreadContext(){if(mysql_thread_init()!=0)throw DatabaseError("Cannot initialize database thread");}
            ~ThreadContext(){mysql_thread_end();}
        };
        thread_local ThreadContext thread_context;
        std::unique_lock<std::mutex> lock(mutex_);
        if(!initialized_)throw DatabaseError("Database pool not initialized");
        if(!condition_.wait_for(lock,std::chrono::seconds(3),[this]{return !pool_.empty();}))throw DatabaseError("Database pool acquisition timed out");
        auto connection=std::move(pool_.front());pool_.pop();lock.unlock();
        if(!connection.alive()){
            auto replacement=create_connection();
            if(!replacement){give_back(std::move(connection));throw DatabaseError("Cannot reconnect database connection");}
            connection=std::move(*replacement);
        }
        return Lease(this,std::move(connection));
    }
private:
    Config config_;std::queue<MysqlConn> pool_;std::mutex mutex_;std::condition_variable condition_;bool initialized_=false;
    ConnectionPool()=default;
    void give_back(MysqlConn&& connection){
        {std::lock_guard<std::mutex> lock(mutex_);pool_.push(std::move(connection));}
        condition_.notify_one();
    }
    std::optional<MysqlConn> create_connection(){
        MysqlConn connection;
        if(!connection.connect(config_.host.c_str(),config_.user.c_str(),config_.password.c_str(),config_.database.c_str(),config_.port))return std::nullopt;
        return std::optional<MysqlConn>(std::move(connection));
    }
};
} // namespace lms
#endif
