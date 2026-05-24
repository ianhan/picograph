#include "picomem/module.h"

namespace picomem {

const Module &register_view_module();
const Module &mda_module();
const Module &sample_module();

const Module &active_module() {
#if PICOMEM_MODULE_REGISTER_VIEW
    return register_view_module();
#elif PICOMEM_MODULE_MDA
    return mda_module();
#elif PICOMEM_MODULE_SAMPLE
    return sample_module();
#else
#error "No PICOMEM_MODULE selected"
#endif
}

}  // namespace picomem
