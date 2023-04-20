#include "RadixSort_Test.h"
#include"secure-join/RadixSort.h"
#include "cryptoTools/Network/IOService.h"

using namespace oc;
using namespace secJoin;


void share(const Matrix<u32>& x, Matrix<u32>& x0, Matrix<u32>& x1, PRNG& prng)
{
    x0.resize(x.rows(), x.cols());
    x1.resize(x.rows(), x.cols());
    prng.get(x0.data(), x0.size());
    //for (u64 i = 0; i < x.size(); ++i)
    //{
    //    x0(i) = x(i) / 2;
    //}
    for (u64 i = 0; i < x.size(); ++i)
        x1(i) = x(i) - x0(i);
}



void share(const Matrix<u8>& x, u64 bitCount, Matrix<u8>& x0, Matrix<u8>& x1, PRNG& prng)
{
    x0.resize(x.rows(), x.cols());
    x1.resize(x.rows(), x.cols());
    prng.get(x0.data(), x0.size());
    for (u64 i = 0; i < x.size(); ++i)
        x1(i) = x(i) ^ x0(i);

    if (bitCount % 8)
    {
        auto mask = (1 << (bitCount % 8)) - 1;
        for (u64 i = 0; i < x.rows(); ++i)
        {
            x0[i].back() &= mask;
            x1[i].back() &= mask;
        }
    }
}

void share(const Matrix<u8>& x, Matrix<u8>& x0, Matrix<u8>& x1, PRNG& prng)
{
    share(x, x.cols() * 8, x0, x1, prng);
}
Perm reveal(const ComposedPerm& x0, const ComposedPerm& x1)
{
    Perm p(x0.size());
    for (u64 i = 0; i < p.size(); ++i)
        p.mPerm[i] = x0.mPerm[i] + x1.mPerm[i];

    //p.validate();
    return p;
}

Matrix<u32> reveal(const Matrix<u32>& x0, const Matrix<u32>& x1)
{
    Matrix<u32> r(x0.rows(), x0.cols());
    for (u64 i = 0; i < r.size(); ++i)
        r(i) = x0(i) + x1(i);
    return r;
}


Matrix<u8> reveal(const Matrix<u8>& x0, const Matrix<u8>& x1)
{
    Matrix<u8> r(x0.rows(), x0.cols());
    for (u64 i = 0; i < r.size(); ++i)
        r(i) = x0(i) ^ x1(i);
    return r;
}

void RadixSort_aggregateSum_test()
{
    u64 n = 123;
    u64 L = 16;

    oc::Matrix<u32> f(n, L);
    oc::Matrix<u32> s1(n, L);
    oc::Matrix<u32> s2(n, L);
    u64 partyIdx = 0;

    {
        auto L2 = f.cols();
        auto m = f.rows();

        // sum = -1
        u32 sum = -partyIdx;

        // sum over column j.
        for (u64 j = 0; j < L2; ++j)
        {
            auto f0 = f.begin() + j;
            auto s0 = s1.begin() + j;
            for (u64 i = 0; i < m; ++i)
            {
                sum += *f0;
                *s0 = sum;
                f0 += L2;
                s0 += L2;
            }
        }
    }

    RadixSort::aggregateSum(f, s2, 0);

    for (u64 i = 0; i < s1.size(); ++i)
        if (s1(i) != s2(i))
            throw RTE_LOC;
}

void RadixSort_hadamardSum_test()
{
    auto comm = coproto::LocalAsyncSocket::makePair();
    u64 cols = 5;
    u64 rows = 10;

    oc::Matrix<u32> d0(rows, rows), d1(rows, rows);


    PRNG prng(block(0, 0));
    RadixSort s0(0), s1(1);

    oc::Matrix<u32> l(rows, cols), r(rows, cols);

    oc::Matrix<u32> l0(rows, cols), l1(rows, cols);
    oc::Matrix<u32> r0(rows, cols), r1(rows, cols);
    //oc::Matrix<u32> c0(rows, cols), c1(rows, cols);
    ComposedPerm p0(rows,0), p1(rows, 1);

    for (u64 i = 0; i < rows; ++i)
    {
        for (u64 j = 0; j < l[i].size(); ++j)
        {

            r(i, j) = prng.get<u64>() % 4 + 1;
            l(i, j) = prng.get<u64>() % 4 + 1;
        }
    }
    //l1 = l;
    //r1 = r;

    share(l, l0, l1, prng);
    share(r, r0, r1, prng);

    OleGenerator g0, g1;
    g0.fakeInit(OleGenerator::Role::Sender);
    g1.fakeInit(OleGenerator::Role::Receiver);

    macoro::sync_wait(macoro::when_all_ready(
        s0.hadamardSum(l0, r0, p0, g0, comm[0]),
        s1.hadamardSum(l1, r1, p1, g1, comm[1])
    ));

    Perm ff = reveal(p0, p1);
    //auto c = reveal(c0, c1);

    oc::Matrix<u32> exp(rows, 1);
    exp.setZero();

    for (u64 i = 0; i < cols; ++i)
    {
        for (u64 j = 0; j < (u64)exp.size(); ++j)
        {
            //if (c(j, i) != r(j, i) * l(j, i))
            //    throw RTE_LOC;

            exp(j) += r(j, i) * l(j, i);
        }
    }

    for (u64 i = 0; i < rows; ++i)
        if (exp(i) != ff[i])
        {
            //std::cout << exp << "\n\n" << ff << std::endl;
            throw RTE_LOC;
        }
}

void RadixSort_oneHot_test()
{

    u64 L = 2;
    u64 n = 324;
    u64 mod = 1ull << L;
    auto comm = coproto::LocalAsyncSocket::makePair();
    std::array<std::future<void>, 2> f;
    std::array<OleGenerator, 2> g;
    g[0].fakeInit(OleGenerator::Role::Sender);
    g[1].fakeInit(OleGenerator::Role::Receiver);

    oc::PRNG prng(oc::ZeroBlock);
    Matrix<u8> kk(n, 1);
    for (u64 i = 0; i < n; ++i)
        kk(i) = prng.get<u8>() % mod;

    std::array<oc::Matrix<u8>, 2> k, bits;

    share(kk, k[0], k[1], prng);
    auto cir = RadixSort::indexToOneHotCircuit(L);

    std::array<Gmw, 2> gmw;
    gmw[0].init(n, cir, g[0]);
    gmw[1].init(n, cir, g[1]);

    gmw[0].setInput(0, k[0]);
    gmw[1].setInput(0, k[1]);

    macoro::sync_wait(macoro::when_all_ready(
        gmw[0].run(comm[0]),
        gmw[1].run(comm[1])
    ));

    bits[0].resize(n, 1);
    bits[1].resize(n, 1);
    gmw[0].getOutput(0, bits[0]);
    gmw[1].getOutput(0, bits[1]);

    auto bb = reveal(bits[0], bits[1]);

    for (u64 i = 0; i < n; ++i)
    {
        oc::BitIterator iter((u8*)&bb(i, 0));
        for (u64 j = 0; j < mod; ++j, ++iter)
        {
            auto exp = kk(i) == j ? 1 : 0;
            auto act = *iter;
            if (exp != act)
                throw RTE_LOC;
        }
    }
}


void RadixSort_bitInjection_test()
{

    auto comm = coproto::LocalAsyncSocket::makePair();
    u64 L = 21;
    u64 n = 128 * 7;


    PRNG prng(block(0, 0));

    oc::Matrix<u8> k(n, oc::divCeil(L, 8));
    oc::Matrix<u8> k0(n, k.cols()), k1(n, k.cols());
    oc::Matrix<u32> f0(n, L), f1(n, L);

    for (u64 i = 0; i < k.size(); ++i)
        k(i) = prng.get();
    if (L % 8)
    {
        auto r = L % 8;
        auto mask = (1 << r) - 1;
        for (u64 i = 0; i < k.rows(); ++i)
            k[i].back() &= mask;

    }
    OleGenerator g0, g1;
    g0.fakeInit(OleGenerator::Role::Receiver);
    g1.fakeInit(OleGenerator::Role::Sender);

    share(k, L, k0, k1, prng);

    macoro::sync_wait(macoro::when_all_ready(
        bitInjection(L, k0, 32, f0, g0, comm[0]),
        bitInjection(L, k1, 32, f1, g1, comm[1])
    ));

    auto ff = reveal(f0, f1);
    if ((u64)ff.rows() != n)
        throw RTE_LOC;
    if ((u64)ff.cols() != L)
        throw RTE_LOC;

    for (u64 i = 0; i < k.rows(); ++i)
    {
        auto iter = oc::BitIterator(k[i].data());
        for (u64 j = 0; j < L; ++j)
        {
            auto v = ff(i, j);
            auto b = *iter++;
            if (v != b)
            {
                throw RTE_LOC;
            }
        }
    }
}

void RadixSort_genValMasks2_test()
{

    auto comm = coproto::LocalAsyncSocket::makePair();
    //u64 L = 1;
    //u64 n = 128 * 8;

    for (auto n : { 10, 324,3242 })
    {
        for (auto L : { 1 , 2 ,5 })
        {

            PRNG prng(block(0, 0));
            RadixSort s0(0), s1(1);

            oc::Matrix<u8> k(n, oc::divCeil(L, 8));
            oc::Matrix<u8> k0(n, oc::divCeil(L, 8)), k1(n, oc::divCeil(L, 8));
            oc::Matrix<u32> f0(n, 1ull << L), f1(n, 1ull << L);

            std::vector<std::vector<i64>> vals(1 << L);
            for (u64 i = 0; i < k.rows(); ++i)
            {
                u64 v = prng.get<u64>() & ((1 << L) - 1);
                k(i) = v;
                vals[v].push_back(i);
            }

            OleGenerator g0, g1;
            g0.fakeInit(OleGenerator::Role::Receiver);
            g1.fakeInit(OleGenerator::Role::Sender);

            share(k, L, k0, k1, prng);

            macoro::sync_wait(macoro::when_all_ready(
                s0.genValMasks2(L, k0, f0, g0, comm[0]),
                s1.genValMasks2(L, k1, f1, g1, comm[1])
            ));

            auto ff = reveal(f0, f1);
            if ((u64)ff.rows() != n)
                throw RTE_LOC;
            if ((u64)ff.cols() != (1ull << L))
                throw RTE_LOC;

            //for (u64 ii = 0; ii < k.size(); ++ii)
            //    std::cout << (int)k(ii) << " ";

            //std::cout << std::endl;

            //for (u64 i = 0; i < ff.rows(); ++i)
            //{
            //    std::cout << std::endl;
            //    for (u64 j = 0; j < ff.cols(); ++j)
            //        std::cout << ff(i, j) << " ";
            //}
            //std::cout << std::endl;

            for (u64 i = 0; i < (1ull << L); ++i)
            {


                for (u64 j = 0; j < n; ++j)
                {
                    if ((u64)k(j) == i)
                    {
                        auto ee = ff(j, i);
                        if (ee != 1)
                            throw RTE_LOC;
                    }
                    else
                    {
                        if (ff(j, i) != 0)
                        {

                            throw RTE_LOC;
                        }
                    }
                }
            }

        }
    }

}

void RadixSort_genBitPerm_test()
{

    auto comm = coproto::LocalAsyncSocket::makePair();

    //u64 L = 4;
    //u64 n = 40;
    u64 trials = 5;
    for (auto m : { 3, 10, 15 })
    	for (auto n : { 10, 40, 1000 })
    	{
    		for (auto L : { 1,3,5 })
    		{
    			for (u64 tt = 1; tt < trials; ++tt)
    			{
    				PRNG prng(block(tt, 0));
    				RadixSort s[2];
    				std::vector<ComposedPerm> p[2];

    				assert(m < 64);
    				oc::Matrix<u8> k(n, oc::divCeil(m,8));
    				oc::Matrix<u8> sk[2];
    				std::future<void> f[3];
    				//m = L;
    				auto ll = oc::divCeil(m, L);
    				std::vector<Perm> exp(ll);
    				std::vector<oc::Matrix<i64>> ke(ll);
    				for (u64 i = 0; i < 3; ++i)
    				{
    					p[i].resize(ll);
    					s[i].init(i);
    					sk[i].resize(n, m);
    				}

    				for (u64 j = 0; j < ll; ++j)
    				{
    					auto jj = ll - 1 - j;
    					auto shift = std::min<u64>(L, m - L * jj);
    					auto mask = ((1ull << shift) - 1);

    					ke[jj].resize(n, 1);
    					std::vector<std::vector<i64>> vals(1 << L);
    					for (u64 i = 0; i < k.rows(); ++i)
    					{
    						u64 v = prng.get<u64>() & mask;

    						ke[jj](i) = v;
    						k(i) = k(i) << shift | v;

    						vals[v].push_back(i);
    					}
    					for (u64 i = 0; i < vals.size(); ++i)
    						exp[jj].mPerm.insert(exp[jj].mPerm.end(), vals[i].begin(), vals[i].end());

    					exp[jj] = exp[jj].inverse();
    				}

    				share(k, m, sk[0], sk[1], prng);


    				for (u64 j = 0; j < ll; ++j)
    				{
    					oc::Matrix<u8> kk[2];
    					for (u64 i = 0; i < 3; ++i)
    						kk[i] = s[i].extract(j * L, L, sk[i]);
    					auto kka = reveal(kk[0], kk[1]);
    					auto d0 = kka.data();
    					auto d1 = ke[j].data();
    					if (
    						kka.size() != ke[j].size() ||
    						kka.rows() != ke[j].rows() ||
    						memcmp(d0, d1, kka.size() * sizeof(i64))
    						)
    					{
    						//print(kka, ke[j], kk->bitCount());
    						throw RTE_LOC;
    					}
    				}

                    throw RTE_LOC;
                    {
                        //for (u64 i = 0; i < 3; ++i)
                        //{
                        //	f[i] = std::async([&, i] {
                        //		for (u64 j = 0; j < ll; ++j)
                        //		{
                        //			auto kk = s[i].extract(j * L, L, sk[i]);
                        //			s[i].genBitPerm(kk, p[i][j], *g[i], comm[i]);
                        //		}
                        //		});
                        //}
                        //for (u64 i = 0; i < 3; ++i)
                        //	f[i].get();

                        //for (u64 j = 0; j < ll; ++j)
                        //{

                        //	aby3::eMatrix<i64> ff = reveal(p[0][j].mShare, p[1][j].mShare, p[2][j].mShare);
                        //	Perm act;
                        //	act.mPerm.insert(act.mPerm.begin(), ff.data(), ff.data() + ff.size());


                        //	if (exp[j] != act)
                        //	{
                        //		std::cout << "\n" << exp[j] << "\n" << act << std::endl;
                        //		throw RTE_LOC;
                        //	}
                        //}
                    }
    			}
    		}
    	}

}


void RadixSort_genPerm_test()
{

    //IOService ios;
    //Channel chl01 = Session(ios, "127.0.0.1:1212", SessionMode::Server, "12").addChannel();
    //Channel chl10 = Session(ios, "127.0.0.1:1212", SessionMode::Client, "12").addChannel();
    //Channel chl02 = Session(ios, "127.0.0.1:1212", SessionMode::Server, "13").addChannel();
    //Channel chl20 = Session(ios, "127.0.0.1:1212", SessionMode::Client, "13").addChannel();
    //Channel chl12 = Session(ios, "127.0.0.1:1212", SessionMode::Server, "23").addChannel();
    //Channel chl21 = Session(ios, "127.0.0.1:1212", SessionMode::Client, "23").addChannel();

    //aby3::CommPkg com0{ chl02, chl01 };
    //aby3::CommPkg com1{ chl10, chl12 };
    //aby3::CommPkg com2{ chl21, chl20 };

    //u64 trials = 5;

    //for (auto n : { 10,100, 1000 })
    //{
    //	for (auto bitCount : {7,17,24})
    //	{
    //		for (auto L : { 3 })
    //		{
    //			for (u64 tt = 0; tt < trials; ++tt)
    //			{

    //				si64Matrix d0(n, n), d1(n, n), d2(n, n);

    //				aby3::Sh3Encryptor e0, e1, e2;
    //				e0.init(0, block(0, 0), block(1, 1));
    //				e1.init(1, block(1, 1), block(2, 2));
    //				e2.init(2, block(2, 2), block(0, 0));
    //				auto& g0 = e0.mShareGen;
    //				auto& g1 = e1.mShareGen;
    //				auto& g2 = e2.mShareGen;

    //				PRNG prng(block(0, 0));
    //				RadixSort s0(0), s1(1), s2(2);
    //				s0.mL = L;
    //				s1.mL = L;
    //				s2.mL = L;
    //				VectorPerm p0, p1, p2;
    //				Perm exp(n);

    //				oc::Matrix<i64> k(n, 1);
    //				aby3::sbMatrix k0, k1, k2;

    //				for (u64 i = 0; i < k.rows(); ++i)
    //				{
    //					u64 v = prng.get<u64>() & ((1ull << bitCount) - 1);
    //					k(i) = v;
    //				}

    //				std::stable_sort(exp.begin(), exp.end(),
    //					[&](const auto& a, const auto& b) {
    //						return (k(a) < k(b));
    //					});
    //				exp = exp.inverse();

    //				share(k, bitCount, k0, k1, k2, prng);

    //				auto fu0 = std::async([&] { s0.genPerm(k0, p0, g0, com0); });
    //				auto fu1 = std::async([&] { s1.genPerm(k1, p1, g1, com1); });
    //				auto fu2 = std::async([&] { s2.genPerm(k2, p2, g2, com2); });

    //				fu0.get();
    //				fu1.get();
    //				fu2.get();

    //				aby3::eMatrix<i64> ff = reveal(p0.mShare, p1.mShare, p2.mShare);
    //				Perm act;
    //				act.mPerm.insert(act.mPerm.begin(), ff.data(), ff.data() + ff.size());


    //				if (exp != act)
    //				{
    //					std::cout << "n " << n << " b " << bitCount << " L " << L << std::endl;
    //					std::cout << "\n" << exp << "\n" << act << std::endl;
    //					throw RTE_LOC;
    //				}

    //			}
    //		}
    //	}
    //}

}
