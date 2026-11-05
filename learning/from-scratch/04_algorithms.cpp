#include "model.hpp"
#include <cassert>
#include <iostream>
#include <stack>
#include <unordered_map>
#include <vector>

int find_order(const std::vector<lesson::Order>& orders,const std::string& id) {
    for(std::size_t i=0;i<orders.size();++i)
        if(orders[i].id==id) return static_cast<int>(i);
    return -1;
}
int binary_find(const std::vector<int>& values,int target) {
    int left=0,right=static_cast<int>(values.size())-1;
    while(left<=right) {
        int middle=left+(right-left)/2;
        if(values[middle]==target)return middle;
        if(values[middle]<target)left=middle+1;
        else right=middle-1;
    }
    return -1;
}
std::vector<int> two_sum(const std::vector<int>& values,int target) {
    std::unordered_map<long long,int> seen;
    for(int i=0;i<static_cast<int>(values.size());++i) {
        auto found=seen.find(static_cast<long long>(target)-values[i]);
        if(found!=seen.end())return {found->second,i};
        seen[values[i]]=i;
    }
    return {};
}
bool brackets(const std::string& text) {
    std::stack<char> opens;
    for(char c:text) {
        if(c=='(' || c=='[' || c=='{'){opens.push(c);continue;}
        char wanted=c==')'?'(':c==']'?'[':c=='}'?'{':'\0';
        if(wanted=='\0' || opens.empty() || opens.top()!=wanted)return false;
        opens.pop();
    }
    return opens.empty();
}
int main() {
    lesson::Order a,b;a.id="A";b.id="B";
    assert(find_order({a,b},"B")==1);
    assert(find_order({},"A")==-1);
    assert(find_order({a,b},"missing")==-1);
    assert(binary_find({},3)==-1);
    assert(binary_find({1,3,5},3)==1);
    assert(binary_find({1,3,5},4)==-1);
    assert(binary_find({1},1)==0);
    assert(two_sum({2,7,11,15},9)==std::vector<int>({0,1}));
    assert(two_sum({3,3},6)==std::vector<int>({0,1}));
    assert(two_sum({1},2).empty());
    assert(brackets("([]){}"));assert(brackets(""));
    assert(!brackets("([)]"));assert(!brackets("]"));assert(!brackets("(("));
    std::cout<<"PASS: linear search, binary search, two sum, bracket boundaries\n";
}
