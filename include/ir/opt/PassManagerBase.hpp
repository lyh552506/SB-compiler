#pragma once
enum PassName {
  mem2reg,
  pre,
  constprop,
  dce,
  adce,
  loops,
  help,
  simplifycfg,
  ece,
  Inline,
  global2local,
  sccp,
  reassociate,
  cse,
  lcssa,
  licm,
  looprotate
};
class PassManagerBase{
public:
    PassManagerBase()=default;
    virtual bool RunOnFunction() = 0;
    virtual void RunOnFunction()=0;
    virtual void InitPass(){};
    virtual void PrintPass()=0;
};