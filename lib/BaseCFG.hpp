#pragma once
#include <vector>
#include <cassert>
#include "List.hpp"
#include <variant>
#include <functional>
#include <iostream>
#include <cxxabi.h>
#include "Type.hpp"
#include <string>
class User;
class Value;
class BasicBlock;
class Use
{
    friend class UserList;
    /// @brief 使用者
    User* fat=nullptr;
    /// @brief 被使用者
    Value* usee=nullptr;
    /// @brief 下一个Use
    Use* nxt=nullptr;
    /// @brief 管理这个Use的指针
    Use** prev=nullptr;
    public:
    Use()=delete;
    Use(User*,Value*);
    /// @brief 注意，调用这个方法的一定是User，所以我加了个鉴权
    void RemoveFromUserList(User* is_valid);
    Value* GetValue();
    
};
/// @brief prepare for Value to quickly find out its User
class UserList
{
    Use* head=nullptr;
    public:
    UserList()=default;
    void push_front(Use* _data);
};
class Value
{
    friend class Module;
    /// @brief 存储所有的User
    UserList userlist;
    protected:
    std::string name;
    Type* tp;
    public:
    virtual ~Value()=default;
    Value()=delete;
    Value(Type*);
    void print();
    InnerDataType GetTypeEnum();
    virtual Type* GetType();
    void add_user(Use* __data);
    virtual bool isConst(){return false;}
    void RAUW(Value* val);
    virtual std::string GetName();
};
using Operand=Value*;
class User:public Value,public list_node<BasicBlock,User>
{
    using UsePtr=std::unique_ptr<Use>;
    protected:
    std::vector<UsePtr> uselist;
    public:
    void add_use(Value* __data);
    virtual void print()=0;
    User();
    User(Type* tp);
    virtual Operand GetDef();
    void ir_mark();
    //virtual void EraseFromBlock();
    //virtual BasicBlock* GetParent();
    std::vector<UsePtr>& Getuselist(){return this->uselist;}
};
class ConstIRInt:public Value
{
    int val;
    public:
    ConstIRInt(int);
    int GetVal();
    bool isConst()final{return true;}
    void ir_mark();
};
class ConstIRFloat:public Value
{
    float val;
    public:
    ConstIRFloat(float);
    float GetVal();
    bool isConst()final{return true;}
    void ir_mark();
};