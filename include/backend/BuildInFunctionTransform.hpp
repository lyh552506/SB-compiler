#include "BackendPass.hpp"
class BuildInFunctionTransform:public BackEndPass<Function>{
    public:
    bool run(Function*) override;
};