//
// Created by Yunming Zhang on 5/9/17.
//

#ifndef GRAPHIT_MIR_LOWER_H
#define GRAPHIT_MIR_LOWER_H

#include <graphit/midend/mir_context.h>
#include <graphit/midend/lower_physical_data_layout.h>

namespace graphit {
    class MIRLower {
    public:
        MIRLower(){};
        
        void lower(MIRContext* mir_context);
        
    };
}


#endif //GRAPHIT_MIR_LOWER_H
