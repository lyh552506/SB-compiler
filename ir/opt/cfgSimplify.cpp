#include "cfgSimplify.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <vector>

#include "BaseCFG.hpp"
#include "CFG.hpp"
#include "Singleton.hpp"

void cfgSimplify::RunOnFunction() {
  bool keep_loop = true;
  while (keep_loop) {
    keep_loop = false;
    keep_loop |= simplify_Block();
    keep_loop |= DealBrInst();
    keep_loop |= DelSamePhis();
    keep_loop |= mergeSpecialBlock();
    keep_loop |= SimplifyUncondBr();
    keep_loop |= mergeRetBlock();
    keep_loop |=simplifyPhiInst();
  }
  m_dom->update();
}

bool cfgSimplify::simplifyPhiInst() {
  bool changed = false;
  for (auto bb : m_func->GetBasicBlock()) {
    for (auto iter = bb->begin(); iter != bb->end(); ++iter) {
      if (auto phi = dynamic_cast<PhiInst*>(*iter)) {
        bool HasUndef = false;
        Value* origin = nullptr;
        auto& incomes = phi->GetAllPhiVal();
        for (auto& income : incomes) {
          if (dynamic_cast<UndefValue*>(income)) {
            HasUndef = true;
            continue;
          }
          if (income == phi) continue;
          if (origin != nullptr && income != origin) break;
          origin = income;
        }
        if (origin == nullptr) {
          auto undef = UndefValue::get(phi->GetType());
          phi->ClearRelation();
          phi->RAUW(undef);
          delete phi;
          changed = true;
          continue;
        }
        if (HasUndef) {
          if (auto inst = dynamic_cast<User*>(origin))
            if (!m_dom->dominates(inst->GetParent(), phi->GetParent()))
              continue;
          phi->RAUW(origin);
          phi->ClearRelation();
          phi->EraseFromParent();
          changed = true;
          continue;
        }
      } else
        break;
    }
  }
  return changed;
}

bool cfgSimplify::mergeSpecialBlock() {
  bool changed = false;
  int index = 0;
  while (index < m_func->GetBasicBlock().size()) {
    auto bb = m_func->GetBasicBlock()[index++];
    int pred_num = std::distance(m_dom->GetNode(bb->num).rev.begin(),
                                 m_dom->GetNode(bb->num).rev.end());
    if (pred_num == 0 || pred_num > 1) continue;
    BasicBlock* pred =
        m_dom->GetNode(m_dom->GetNode(bb->num).rev.front()).thisBlock;
    if (pred == bb) continue;
    // m_dom->update();
    // simplifyPhiInst();

    //确保前驱也只有一个后继
    auto& node = m_dom->GetNode(pred->num);
    int succ_num = std::distance(node.des.begin(), node.des.end());
    if (succ_num != 1 || node.des.front() != bb->num) continue;

    // BasicBlock* nxt = nullptr;
    // if (!dynamic_cast<RetInst*>(bb->back())) {
    //   nxt =
    //   dynamic_cast<BasicBlock*>(bb->back()->Getuselist()[0]->GetValue());
    // }
    if (bb->back()->IsCondInst()) {
      auto cond = dynamic_cast<CondInst*>(bb->back());
      BasicBlock* succ_1 =
          dynamic_cast<BasicBlock*>(cond->Getuselist()[1]->GetValue());
      BasicBlock* succ_2 =
          dynamic_cast<BasicBlock*>(cond->Getuselist()[2]->GetValue());
      if (dynamic_cast<PhiInst*>(succ_1->front()) ||
          dynamic_cast<PhiInst*>(succ_2->front()))
        continue;
      m_dom->GetNode(succ_1->num).rev.remove(bb->num);
      m_dom->GetNode(succ_2->num).rev.remove(bb->num);
      m_dom->GetNode(pred->num).des.remove(bb->num);
      m_dom->GetNode(succ_1->num).rev.push_front(pred->num);
      m_dom->GetNode(succ_2->num).rev.push_front(pred->num);
      m_dom->GetNode(pred->num).des.push_front(succ_1->num);
      m_dom->GetNode(pred->num).des.push_front(succ_2->num);
    } else if (bb->back()->IsUncondInst()) {
      BasicBlock* succ =
          dynamic_cast<BasicBlock*>(bb->back()->Getuselist()[0]->GetValue());
      if (dynamic_cast<PhiInst*>(succ->front())) continue;
      m_dom->GetNode(succ->num).rev.remove(bb->num);
      m_dom->GetNode(pred->num).des.remove(bb->num);
      m_dom->GetNode(succ->num).rev.push_front(pred->num);
      m_dom->GetNode(pred->num).des.push_front(succ->num);
    }
    User* cond = pred->back();
    delete cond;
    //将后续的指令转移
    for (auto i = bb->begin(); i != bb->end(); ++i) {
      (*i)->EraseFromParent();
      pred->push_back(*i);
    }
    int length = m_func->GetBasicBlock().size();
    m_func->GetBasicBlock()[index - 1] = m_func->GetBasicBlock()[length - 1];
    m_func->GetBasicBlock().pop_back();
    index--;
    DeletDeadBlock(bb);
    // if (nxt != nullptr) {
    //   m_dom->GetNode(nxt->num).rev.push_front(pred->num);
    //   m_dom->GetNode(pred->num).des.push_front(nxt->num);
    // }
    changed |= true;
  }
  return changed;
}

void cfgSimplify::updateDTinfo(BasicBlock* bb) {
  auto& node = m_dom->GetNode(bb->num);
  std::vector<int> Erase;
  for (int rev : node.rev) {
    auto& pred = m_dom->GetNode(rev);
    pred.des.remove(bb->num);
  }
  for (int des : node.des) {
    auto& succ = m_dom->GetNode(des);
    succ.rev.remove(bb->num);
  }
}

bool cfgSimplify::SimplifyUncondBr() {
  int index = 0;
  bool changed = false;
  while (index < m_func->GetBasicBlock().size()) {
    BasicBlock* bb = m_func->GetBasicBlock()[index++];
    User* inst = bb->back();
    if (dynamic_cast<UnCondInst*>(inst)) {
      //检查是否是empty block，在这里我们忽略其他phi存在情况，不值得花费太多时间
      if (*(bb->begin()) != inst) continue;
      changed |= SimplifyEmptyUncondBlock(bb);
    } else
      continue;
  }
  return changed;
}

// 传入的bb满足：跳转语句是uncondbr，并且只有uncondinst这一条指令
bool cfgSimplify::SimplifyEmptyUncondBlock(BasicBlock* bb) {
  if (bb->num == m_dom->GetNode(bb->num).idom) return false;
  BasicBlock* succ = dynamic_cast<BasicBlock*>(
      dynamic_cast<UnCondInst*>(bb->back())->Getuselist()[0]->GetValue());
  assert(succ);
  for (int rev : m_dom->GetNode(bb->num).rev) {
    BasicBlock* pred = m_dom->GetNode(rev).thisBlock;
    //杜绝这种情况：
    //      pred
    //      / \
    //     bb  \
    //      \  /
    //      succ
    if (auto cond = dynamic_cast<CondInst*>(pred->back())) {
      BasicBlock* pred_succ_1 =
          dynamic_cast<BasicBlock*>(cond->Getuselist()[1]->GetValue());
      BasicBlock* pred_succ_2 =
          dynamic_cast<BasicBlock*>(cond->Getuselist()[2]->GetValue());
      if (pred_succ_1 == bb && pred_succ_2 == succ ||
          pred_succ_1 == succ && pred_succ_2 == bb)
        return false;
    }
  }
  //走到这里后能保证合并是安全的
  //  ... pred
  //    \ /  \
  //     bb  ...
  //     /
  //   succ
  for (auto iter = succ->begin(); iter != succ->end(); ++iter) {
    //更新succ的phi信息
    User* inst = *iter;
    if (auto phi = dynamic_cast<PhiInst*>(inst)) {
      auto it = std::find_if(
          phi->PhiRecord.begin(), phi->PhiRecord.end(),
          [bb](
              const std::pair<const int, std::pair<Value*, BasicBlock*>>& ele) {
            return ele.second.second == bb;
          });
      if (it == phi->PhiRecord.end()) continue;
      int times = 0;
      for (int rev : m_dom->GetNode(bb->num).rev) {
        if (times == 0)
          it->second.second = m_dom->GetNode(rev).thisBlock;
        else
          phi->updateIncoming(it->second.first, m_dom->GetNode(rev).thisBlock);
        times++;
      }
    } else
      break;
  }
  //更新user关系
  for (auto u = succ->GetUserlist().begin(); u != succ->GetUserlist().end();
       ++u) {
    Use* tmp = *u;
    if (tmp->fat->GetParent() == bb) {
      tmp->fat->EraseFromParent();
    }
  }
  std::vector<std::pair<User*, int>> Erase;
  for (auto iter = bb->GetUserlist().begin(); iter != bb->GetUserlist().end();
       ++iter) {
    Use* tmp = *iter;
    auto pred = tmp->GetUser();
    int index;
    for (index = 0; index < pred->Getuselist().size(); index++)
      if (pred->Getuselist()[index]->GetValue() == bb) {
        Erase.push_back(std::make_pair(pred, index));
        break;
      }
  }
  for (int i = 0; i < Erase.size(); i++) {
    auto& [pred, index] = Erase[i];
    pred->RSUW(index, succ);
  }
  for (int rev : m_dom->GetNode(bb->num).rev) {
    m_dom->GetNode(succ->num).rev.push_front(rev);
    m_dom->GetNode(rev).des.push_front(succ->num);
  }

  DeletDeadBlock(bb);
  return true;
}

bool cfgSimplify::mergeRetBlock() {
  bool changed = false;
  BasicBlock* RetBlock = nullptr;
  int i = 0;
  while (i < m_func->GetBasicBlock().size()) {
    BasicBlock* bbs = m_func->GetBasicBlock()[i++];
    RetInst* ret = dynamic_cast<RetInst*>(bbs->back());
    if (!ret) continue;
    //找到对应的迭代器
    auto ret_iter = bbs->rbegin();
    if (ret != bbs->front()) {
      --ret_iter;
      if (!dynamic_cast<PhiInst*>(*ret_iter) || ret_iter != bbs->begin() ||
          ret->Getuselist().size() == 0 ||
          ret->Getuselist()[0]->GetValue() != *ret_iter)
        continue;
    }
    if (!RetBlock) {
      RetBlock = bbs;
      continue;
    }
    //找到了第二个符合条件的retblock
    if (ret->Getuselist().size() == 0 ||
        ret->Getuselist()[0]->GetValue() ==
            RetBlock->back()->Getuselist()[0]->GetValue()) {
      // bbs->RAUW(RetBlock);
      // BasicBlock* PredBB=nullptr;
      std::vector<BasicBlock*> pred;
      std::vector<std::pair<User*,int>> Erase;
      for (auto list = bbs->GetUserlist().begin();
           list != bbs->GetUserlist().end(); ++list) {
        User* tmp = (*list)->GetUser();
        pred.push_back(tmp->GetParent());
        for(int j=0;j<tmp->Getuselist().size();++j)
          if(tmp->Getuselist()[j]->GetValue()==bbs)
            Erase.push_back(std::make_pair(tmp,j));
      }
      for(auto& [user,index]:Erase)
        user->RSUW(index,RetBlock);
      m_func->GetBasicBlock()[i - 1] =
          m_func->GetBasicBlock()[m_func->GetBasicBlock().size() - 1];
      i--;
      m_func->GetBasicBlock().pop_back();
      changed |= true;
      m_dom->GetNode(RetBlock->num).rev.remove(bbs->num);
      for(auto PredBB:pred){
        m_dom->GetNode(PredBB->num).des.remove(bbs->num);
        m_dom->GetNode(PredBB->num).des.push_front(RetBlock->num);
        m_dom->GetNode(RetBlock->num).rev.push_front(PredBB->num);
      }
      DeletDeadBlock(bbs);
      continue;
    }
    PhiInst* insert = dynamic_cast<PhiInst*>(RetBlock->front());
    if (!insert) {
      insert = PhiInst::NewPhiNode(
          RetBlock->front(), RetBlock,
          RetBlock->back()->Getuselist()[0]->GetValue()->GetType());
      Value* val = RetBlock->back()->Getuselist()[0]->GetValue();
      auto& node = m_dom->GetNode(RetBlock->num);
      for (int rev : node.rev) {
        BasicBlock* pre = m_dom->node[rev].thisBlock;
        insert->updateIncoming(val, pre);
      }
      RetBlock->back()->RSUW(0, insert);
    }
    insert->updateIncoming(ret->Getuselist()[0]->GetValue(), bbs);
    delete ret;
    UnCondInst* uncond = new UnCondInst(RetBlock);
    bbs->push_back(uncond);
    changed |= true;
    m_dom->GetNode(RetBlock->num).rev.push_front(bbs->num);
    m_dom->GetNode(bbs->num).des.push_front(RetBlock->num);
  }
  return changed;
}

bool cfgSimplify::DelSamePhis() {
  int i = 0;
  bool changed = false;
  while (i < m_func->GetBasicBlock().size()) {
    BasicBlock* bb = m_func->GetBasicBlock()[i++];
    std::vector<PhiInst*>& phis = BlockToPhis[bb];
    for (auto iter = bb->begin(); iter != bb->end(); ++iter) {
      if (auto phi = dynamic_cast<PhiInst*>(*iter)) {
        if (std::find(phis.begin(), phis.end(), phi) == phis.end())
          phis.push_back(phi);
      } else
        break;
    }
    if (phis.empty()) continue;
    //查找是否有相同phiinst
    for (int i = 0; i < phis.size(); ++i)
      for (int j = i + 1; j < phis.size(); ++j)
        if (phis[i]->IsSame(phis[j])) {
          phis[j]->RAUW(phis[i]);
          delete phis[j];
          //替换了phi后可能会对其他的phi造成影响，此处我们记录并在后续再进行循环
          i--;
          changed = true;
        }
  }
  return changed;
}

bool cfgSimplify::simplify_Block() {
  bool changed = false;
  int index = 0;
  while (index < m_func->GetBasicBlock().size()) {
    BasicBlock* bb = m_func->GetBasicBlock()[index++];
    int pred_num = std::distance(m_dom->GetNode(bb->num).rev.begin(),
                                 m_dom->GetNode(bb->num).rev.end());
    if ((pred_num == 0 && m_dom->GetNode(bb->num).idom != bb->num) ||
        (pred_num == 1 && m_dom->GetNode(bb->num).rev.front() == bb->num)) {
      DeletDeadBlock(bb);
      m_func->GetBasicBlock()[index - 1] =
          m_func->GetBasicBlock()[m_func->GetBasicBlock().size() - 1];
      m_func->GetBasicBlock().pop_back();
      index--;
      changed = true;
      continue;
    }
  }
  return changed;
}

bool cfgSimplify::DealBrInst() {
  for (auto bb : m_func->GetBasicBlock()) {
    User* x = bb->back();
    if (auto cond = dynamic_cast<CondInst*>(x)) {
      auto Bool =
          dynamic_cast<ConstIRBoolean*>(cond->Getuselist()[0]->GetValue());
      BasicBlock* pred = bb;
      BasicBlock* nxt =
          dynamic_cast<BasicBlock*>(cond->Getuselist()[1]->GetValue());
      BasicBlock* ignore =
          dynamic_cast<BasicBlock*>(cond->Getuselist()[2]->GetValue());
      if (Bool != nullptr) {
        if (Bool->GetVal() == true) {
          UnCondInst* uncond = new UnCondInst(nxt);
          auto tmp = pred->begin();
          for (auto iter = pred->begin();; ++iter) {
            if (iter == pred->end()) break;
            tmp = iter;
          }
          tmp.insert_before(uncond);
          delete cond;
          ignore->RemovePredBB(pred);
          //更新m_dom相关参数
          m_dom->GetNode(ignore->num).rev.remove(pred->num);
          m_dom->GetNode(pred->num).des.remove(ignore->num);
        } else {
          UnCondInst* uncond = new UnCondInst(ignore);
          auto tmp = pred->begin();
          for (auto iter = pred->begin();; ++iter) {
            if (iter == pred->end()) break;
            tmp = iter;
          }
          tmp.insert_before(uncond);
          delete cond;
          nxt->RemovePredBB(pred);
          m_dom->GetNode(nxt->num).rev.remove(pred->num);
          m_dom->GetNode(pred->num).des.remove(nxt->num);
        }
        return true;
      } else if (nxt == ignore) {
        UnCondInst* uncond = new UnCondInst(nxt);
        auto tmp = pred->begin();
        for (auto iter = pred->begin();; ++iter) {
          if (iter == pred->end()) break;
          tmp = iter;
        }
        tmp.insert_before(uncond);
        delete cond;
        return true;
      }
    }
  }
  return false;
}

void cfgSimplify::DeletDeadBlock(BasicBlock* bb) {
  auto& node = m_dom->GetNode(bb->num);
  //维护后续的phi关系
  for (int des : node.des) {
    BasicBlock* succ = m_dom->GetNode(des).thisBlock;
    succ->RemovePredBB(bb);
  }
  for (auto iter = bb->begin(); iter != bb->end(); ++iter) {
    User* inst = *iter;
    inst->RAUW(UndefValue::get(inst->GetType()));
  }
  updateDTinfo(bb);
  bb->Delete();
  m_dom->updateBlockNum()--;
}

void cfgSimplify::PrintPass() {
#ifdef SYSY_MIDDLE_END_DEBUG
  std::cout << "-------cfgsimplify--------\n";
  Singleton<Module>().Test();
#endif
}