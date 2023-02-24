#include "Permutation.h"


namespace secJoin
{

    // returns the inverse permutation
    Perm Perm::inverse() const
    {
        std::vector<u64> retd(size());
        for (auto i = 0ull; i < size(); i++)
            retd[mPerm[i]] = i;
            
        return retd;
    }


    // A.compose(B) computes the permutation AoB
    Perm Perm::compose(const Perm& rhs) const
    {
        //std::vector<i64> res = applyInv(rhs.mPerm);
        //return Perm(res);

        return rhs.apply(mPerm);
    }

}