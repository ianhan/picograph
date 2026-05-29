#include "picograph/module.h"

namespace picograph {

const Module &register_view_module();
const Module &hercules_module();
const Module &ega_module();
const Module &vga_module();
const Module &mda_module();
const Module &sample_module();

const Module &active_module() {
#if PICOGRAPH_MODULE_REGISTER_VIEW
    return register_view_module();
#elif PICOGRAPH_MODULE_HERCULES
    return hercules_module();
#elif PICOGRAPH_MODULE_EGA
    return ega_module();
#elif PICOGRAPH_MODULE_VGA
    return vga_module();
#elif PICOGRAPH_MODULE_MDA
    return mda_module();
#elif PICOGRAPH_MODULE_SAMPLE
    return sample_module();
#else
#error "No PICOGRAPH_MODULE selected"
#endif
}

}  // namespace picograph
