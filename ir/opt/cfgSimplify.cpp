#include "cfgSimplify.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>

#include "BaseCFG.hpp"
#include "CFG.hpp"
#include "Singleton.hpp"

void cfgSimplify::RunOnFunction() {
  bool keep_loop = true;
  mergeRetBlock();
  while (keep_loop) {
    keep_loop = false;
    keep_loop |= simplify_Block();
    keep_loop |= DealBrInst();
    keep_loop |= DelSamePhis();
    keep_loop |= mergeSpecialBlock();
  }
  m_dom->update();
}

bool cfgSimplify::mergeSpecialBlock() {
  int index = 0;
  while (index < m_func->GetBasicBlock().size()) {
    auto bb = m_func->GetBasicBlock()[index++];
    int pred_num = std::distance(m_dom->GetNode(bb->num).rev.begin(),
                                 m_dom->GetNode(bb->num).rev.end());
    if (pred_num == 0 || pred_num > 1) continue;
    BasicBlock* pred =
        m_dom->GetNode(m_dom->GetNode(bb->num).rev.front()).thisBlock;
    if (pred == bb) continue;
    ///@warning 此处不考虑单个前驱依然存在phi函数的情况

    //确保前驱也只有一个后继
    auto& node = m_dom->GetNode(pred->num);
    int succ_num = std::distance(node.des.begin(), node.des.end());
    if (succ_num != 1 || node.des.front() != bb->num) continue;
    bb->RAUW(pred);
    User* cond = pred->back();
    cond->ClearRelation();
    cond->EraseFromParent();
    delete cond;
    //将后续的指令转移
    for (auto i = bb->begin(); i != bb->end(); ++i) {
      pred->push_back(*i);
    }
    int length = m_func->GetBasicBlock().size();
    m_func->GetBasicBlock()[index - 1] = m_func->GetBasicBlock()[length - 1];
    m_func->GetBasicBlock().pop_back();
    bb->Clear();
    bb->EraseFromParent();
    //更新m_dom相关参数
    updateDTinfo(bb);
    return true;
  }
  return false;
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

bool cfgSimplify::SimplifyUncondBr(BasicBlock* bb) {
  int index = 0;
  bool changed = false;
  while (index < m_func->GetBasicBlock().size()) {
    BasicBlock* bb = m_func->GetBasicBlock()[index++];
    User* inst = bb->back();
    if (dynamic_cast<UnCondInst*>(inst)) {
      //检查是否是empty block，在这里我们忽略其他phi存在情况，不值得花费太多时间
      if (*(bb->begin()) != inst) continue;
      changed |= SimplifyEmptyUncondBlock(bb);
    }
  }
  return changed;
}

// 传入的bb满足：跳转语句是uncondbr，并且只有uncondinst这一条指令
bool cfgSimplify::SimplifyEmptyUncondBlock(BasicBlock* bb) {
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
  bb->Clear();
  bb->EraseFromParent();
  return true;
}

void cfgSimplify::mergeRetBlock() {
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
      bbs->RAUW(RetBlock);
      bbs->Clear();
      bbs->EraseFromParent();
      //更新m_dom相关参数
      updateDTinfo(bbs);
      m_func->GetBasicBlock()[i - 1] =
          m_func->GetBasicBlock()[m_func->GetBasicBlock().size() - 1];
      m_func->GetBasicBlock().pop_back();
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
    ret->EraseFromParent();
    UnCondInst* uncond = new UnCondInst(RetBlock);
    bbs->push_back(uncond);
  }
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
          phis[j]->ClearRelation();
          phis[j]->RAUW(phis[i]);
          phis[j]->EraseFromParent();
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
      bb->Clear();
      bb->EraseFromParent();
      //更新m_dom相关参数
      updateDTinfo(bb);
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
          cond->ClearRelation();
          cond->EraseFromParent();
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
          cond->ClearRelation();
          cond->EraseFromParent();
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
        cond->ClearRelation();
        cond->EraseFromParent();
        return true;
      }
    }
  }
  return false;
}

void cfgSimplify::PrintPass() {
  std::cout << "-------cfgsimplify--------\n";
  Singleton<Module>().Test();
}