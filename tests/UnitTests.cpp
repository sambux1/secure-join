

#include "cryptoTools/Common/Log.h"
#include <functional>
#include "UnitTests.h"

#include "LowMCPerm_Test.h"
#include "PaillierPerm_Test.h"

namespace secJoin_Tests
{
    oc::TestCollection Tests([](oc::TestCollection& t) {
        
        t.add("PaillierPerm_basic_test       ", PaillierPerm_basic_test);
        t.add("LowMCPerm_perm_test       ", LowMCPerm_perm_test);
        t.add("LowMCPerm_inv_perm_test       ", LowMCPerm_inv_perm_test);
        t.add("LowMCPerm_secret_shared_input_perm_test       ", LowMCPerm_secret_shared_input_perm_test);
        t.add("LowMCPerm_secret_shared_input_inv_perm_test       ", LowMCPerm_secret_shared_input_inv_perm_test);
        t.add("LowMCPerm_replicated_perm_test       ", LowMCPerm_replicated_perm_test);
        

    });
}
