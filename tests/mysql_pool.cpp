#ifdef LMS_HAS_MYSQL
#include "lms/connection_pool.h"
#include "lms/config.h"
#include <future>
#include <iostream>
#include <thread>
#include <stdexcept>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
 try{
  if(argc!=2)throw std::invalid_argument("Usage: mysql-pool-test CONFIG");
  const auto config=lms::AppConfig::load(argv[1]);
  auto& pool=lms::ConnectionPool::instance();
  pool.init({config.mysql.host,config.mysql.user,config.mysql.password,config.mysql.database,config.mysql.port,1});
  {
   auto lease=pool.acquire();
   auto original=lease->prepare("SELECT ?,?,?,?,NULL");
   original.bind_string(0,std::string(600,'x'));original.bind_int(1,123456);original.bind_double(2,4.25);original.bind_string(3,"tail");
   lms::Stmt moved=std::move(original);lms::Stmt assigned;assigned=std::move(moved);
   require(assigned.execute()&&assigned.store_result(),"prepared execution failed");
   std::vector<std::string> row;require(assigned.fetch_row(row),"row missing");
   require(row[0]==std::string(600,'x') && row[1]=="123456" && row[2]=="4.25" && row[3]=="tail" && row[4].empty(),"parameter lifetime, move or result data mismatch");
  }
  std::cout<<"PASS: stable mixed bindings, statement move and long/null results\n";
  {
   auto lease=pool.acquire();lease->exec("CREATE TABLE transaction_probe(id INT PRIMARY KEY,value INT NOT NULL) ENGINE=InnoDB");
   lease->exec("INSERT INTO transaction_probe VALUES(1,0)");
   try{lms::Transaction transaction(*lease.operator->());lease->exec("UPDATE transaction_probe SET value=7 WHERE id=1");throw std::runtime_error("injected business failure");}catch(const std::runtime_error&){}
   auto read=lease->prepare("SELECT value FROM transaction_probe WHERE id=1");read.execute();read.store_result();std::vector<std::string> row;require(read.fetch_row(row)&&row[0]=="0","rollback failed");
   {lms::Transaction transaction(*lease.operator->());lease->exec("UPDATE transaction_probe SET value=9 WHERE id=1");transaction.commit();}
   auto committed=lease->prepare("SELECT value FROM transaction_probe WHERE id=1");committed.execute();committed.store_result();require(committed.fetch_row(row)&&row[0]=="9","commit failed");
  }
  std::cout<<"PASS: transaction rollback on exception and explicit commit\n";
  std::future<bool> waiter;
  {
   auto held=pool.acquire();
   waiter=std::async(std::launch::async,[&pool]{auto lease=pool.acquire();return lease.alive();});
   require(waiter.wait_for(std::chrono::milliseconds(150))==std::future_status::timeout,"pool exceeded its capacity");
  }
  require(waiter.wait_for(std::chrono::seconds(2))==std::future_status::ready && waiter.get(),"returned connection did not wake waiter");
  std::cout<<"PASS: capacity-one pool contention and return notification\n";
  {
   auto held=pool.acquire();auto exhausted=std::async(std::launch::async,[&pool]{try{auto lease=pool.acquire();return false;}catch(const lms::DatabaseError&){return true;}});
   require(exhausted.wait_for(std::chrono::seconds(5))==std::future_status::ready&&exhausted.get(),"pool exhaustion did not time out");
  }
  std::cout<<"PASS: exhausted pool returns an error instead of hanging\n";
  return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
#endif
