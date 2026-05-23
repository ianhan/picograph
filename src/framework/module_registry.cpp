#include "picomem/module.h"

namespace picomem {

const Module &template_module();

const Module &active_module() {
#if PICOMEM_MODULE_TEMPLATE
    return template_module();
#else
#error "No PICOMEM_MODULE selected"
#endif
}

}  // namespace picomem
