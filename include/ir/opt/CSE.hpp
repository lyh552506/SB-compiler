#include "dominant.hpp"
#include "CFG.hpp"
#include "my_stl.hpp"
#include <unordered_set>

namespace HashTool
{
    struct InstHash
    {
        size_t operator()(User* inst)
        {
            size_t HashValue;
            HashValue += std::hash<User::OpID>{}(inst->GetInstId());
            for(auto& Use_ : inst->Getuselist())
            {
                Value* Val = Use_->usee;
                HashValue = HashValue * 111 + std::hash<Value*>{}(Val);
            }
            return HashValue;
        }
    };

    struct Same
    {
        bool operator()(User* LHS, User* RHS) const
        {
            if(LHS->GetInstId() != RHS->GetInstId())
                return false;
            if(LHS->GetType() != RHS->GetType())
                return false;
            auto& LHSUseList = LHS->Getuselist();
            auto& RHSUseList = RHS->Getuselist();
            if(LHSUseList.size() != RHSUseList.size())
                return false;
            if((LHS->GetInstId() > 7 && LHS->GetInstId() < 11) ||  (LHS->GetInstId() > 12 && LHS->GetInstId() < 17))
                return std::is_permutation(LHSUseList.begin(), LHSUseList.end(), RHSUseList.begin(), []
                    (std::unique_ptr<Use>& lhs, std::unique_ptr<Use>& rhs){ return lhs->usee == rhs->usee; });
            return std::equal(LHSUseList.begin(), LHSUseList.end(), RHSUseList.begin(), []
            (std::unique_ptr<Use>& lhs, std::unique_ptr<Use>& rhs){ return lhs->usee == rhs->usee; });
        }
    };
}


class CSE
{

    typedef struct CSE_Info
    {
        std::map<std::tuple<Value*, Value*, User::OpID>, std::pair<size_t, Value*>> AEB_Binary;
        std::map<std::tuple<Value*, ConstantData*, User::OpID>, std::pair<size_t, Value*>> AEB_Const_RHS;
        std::map<std::tuple<ConstantData*, Value*, User::OpID>, std::pair<size_t, Value*>> AEB_Const_LHS;

        std::map<Value*, Value*> Loads;
        std::map<std::tuple<Value*, size_t, User::OpID>, Value*> Geps;
        std::set<Function*> Funcs;
    }info;

    std::set<dominance::Node*> Processed;

    std::map<BasicBlock*, info*> BlockOut;
    std::map<BasicBlock*, info*> BlockIn;
    std::vector<User*> wait_del;
    Function* func;
    dominance* DomTree;
private:
    void Init();
    bool RunOnNode(dominance::Node* node, info& block_in, info& block_out);
    Function* Find_Change(Value* val, info* Info);
public:
    CSE(Function* m_func, dominance* dom) : func(m_func), DomTree(dom) { Init(); }
    bool RunOnFunc();
};

