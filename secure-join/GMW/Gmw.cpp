#include "Gmw.h"
#include "cryptoTools/Network/IOService.h"
#include "cryptoTools/Network/Session.h"
#include "cryptoTools/Common/Defines.h"
#include <numeric>
#include "libOTe/Tools/Tools.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include <array>

namespace secJoin
{
    using PRNG = oc::PRNG;
    void Gmw::init(
        u64 n,
        const BetaCircuit& cir,
        OleGenerator& gen)
    {
        mIdx = gen.mRole == OleGenerator::Role::Sender ? 1 : 0;
        mN = n;
        mGen = &gen;

        mRoundIdx = 0;
        mCir = cir;

        if (mCir.mLevelCounts.size() == 0)
            mCir.levelByAndDepth(mLevelize);

        mNumRounds = mCir.mLevelCounts.size();


        mGates = mCir.mGates;
        mWords.resize(mCir.mWireCount, oc::divCeil(mN, 128));

        memset(mWords.data(), 0, mWords.size() * sizeof(*mWords.data()));

        mPrng.SetSeed(gen.mPrng.get());
        mPhiPrng.SetSeed(mPrng.get(), mWords.cols());

        mNumOts = mWords.cols() * 128 * mCir.mNonlinearGateCount * 2;

        mPrint = mCir.mPrints.begin();
    }

    void Gmw::implSetInput(u64 i, oc::MatrixView<u8> input, u64 alignment)
    {
        oc::MatrixView<u8> memView = getInputView(i);

        auto numWires = memView.rows();

        auto bits = alignment * 8;

        // inputs should be the right alignment.
        auto exp = (numWires + bits - 1) / bits;
        auto act = input.cols() / alignment;
        if (exp != act)
            throw RTE_LOC;

        if (input.rows() != mN)
            throw RTE_LOC;

        oc::transpose(input, memView);
    }

    void Gmw::setZeroInput(u64 i)
    {
        oc::MatrixView<u8> memView = getInputView(i);
        memset(memView.data(), 0, memView.size());
    }

    coproto::task<> Gmw::run(coproto::Socket& chl)
    {
        MC_BEGIN(coproto::task<>, this, &chl, i = u64{});

        if (mO.mDebug)
        {
            mO.mWords.resize(mWords.rows(), mWords.cols());

            MC_AWAIT(chl.send(coproto::copy(mWords)));
            MC_AWAIT(chl.recv(mO.mWords));

            for (i = 0; i < mWords.size(); i++)
            {
                mO.mWords(i) = mO.mWords(i) ^ mWords(i);
            }
        }

        mRoundIdx = 0;

        for (i = 0; i < numRounds(); ++i)
            MC_AWAIT(roundFunction(chl));

        MC_END();
    }

    void Gmw::implGetOutput(u64 i, oc::MatrixView<u8> out, u64 alignment)
    {
        oc::MatrixView<u8> memView = getOutputView(i);

        auto numWires = memView.rows();
        auto bits = alignment * 8;

        // inputs should be 8 bit or 128 bit aligned.
        if ((numWires + bits - 1) / bits != out.cols() / alignment)
            throw RTE_LOC;

        if (out.rows() != mN)
            throw RTE_LOC;

        oc::transpose(memView, out);
    }

    oc::MatrixView<u8> Gmw::getInputView(u64 i)
    {
        if (mCir.mInputs.size() <= i)
            throw RTE_LOC;

        auto& inWires = mCir.mInputs[i];

        return getMemView(inWires);
    }

    oc::MatrixView<u8> Gmw::getOutputView(u64 i)
    {
        if (mCir.mOutputs.size() <= i)
            throw RTE_LOC;

        auto& wires = mCir.mOutputs[i];

        return getMemView(wires);
    }

    oc::MatrixView<u8> Gmw::getMemView(BetaBundle& wires)
    {
        // we assume the input bundles are contiguous.
        for (u64 j = 1; j < wires.size(); ++j)
            if (wires[j - 1] + 1 != wires[j])
                throw RTE_LOC;

        oc::MatrixView<u8> memView((u8*)mWords[wires[0]].data(), wires.size(), mWords.cols() * 16);
        return memView;
    }

    //void Gmw::genSilentTriples(u64 batchSize, u64 numThreads)
    //{

    //    mSilent.init(mNumOts, batchSize, numThreads, Mode::Dual, mPrng.get());
    //    mSilent.requiredBaseOts()
    //}

//    coproto::task<> Gmw::generateTriple(
//        u64 batchSize,
//        u64 numThreads,
//        coproto::Socket& chl)
//    {
//        MC_BEGIN(coproto::task<>, this, &tg = mSilent, batchSize, numThreads, &chl,
//            A2 = std::vector<block>{},
//            B2 = std::vector<block>{},
//            C2 = std::vector<block>{},
//            D2 = std::vector<block>{},
//            mid = mNumOts / 128 / 2
//        );
//
//        if (mNumOts == 0)
//            MC_RETURN_VOID();
//
//        setTimePoint("Gmw::generateTriple begin");
//        if (mTimer)
//            tg.setTimer(*mTimer);
//
//        tg.init(mNumOts, batchSize, numThreads, mIdx ? Mode::Receiver : Mode::Sender, mPrng.get());
//
//#ifndef SECUREJOIN_ENABLE_FAKE_GEN
//
//        if (tg.hasBaseOts() == false)
//            MC_AWAIT(tg.generateBaseOts(mIdx, mPrng, chl));
//#endif
//
//        MC_AWAIT(tg.expand(chl));
//        if (mIdx)
//        {
//            mA = tg.mMult.subspan(0, mid);
//            mC = tg.mMult.subspan(mid);
//            mC2 = tg.mMult.subspan(mid);
//            mB = tg.mAdd.subspan(0, mid);
//            mD = tg.mAdd.subspan(mid);
//        }
//        else
//        {
//            mA = tg.mMult.subspan(mid);
//            mC = tg.mMult.subspan(0, mid);
//            mC2 = tg.mMult.subspan(0, mid);
//            mB = tg.mAdd.subspan(mid);
//            mD = tg.mAdd.subspan(0, mid);
//        }
//
//        if (mO.mDebug)
//        {
//
//            A2.resize(mA.size());
//            B2.resize(mA.size());
//            C2.resize(mA.size());
//            D2.resize(mA.size());
//
//            MC_AWAIT(chl.send(coproto::copy(mA)));
//            MC_AWAIT(chl.send(coproto::copy(mB)));
//            MC_AWAIT(chl.send(coproto::copy(mC)));
//            MC_AWAIT(chl.send(coproto::copy(mD)));
//
//            MC_AWAIT(chl.recv(A2));
//            MC_AWAIT(chl.recv(B2));
//            MC_AWAIT(chl.recv(C2));
//            MC_AWAIT(chl.recv(D2));
//
//            for (u64 i = 0; i < mA.size(); ++i)
//            {
//                if (neq((mA[i] & C2[i]) ^ mB[i], D2[i]))
//                {
//                    std::cout << "bad triple at 0." << i << ", n " << mNumOts << std::endl;
//                    throw std::runtime_error("");
//                }
//
//                if (neq((A2[i] & mC[i]) ^ B2[i], mD[i]))
//                {
//                    std::cout << "bad triple at 1." << i << ", n " << mNumOts << std::endl;
//                    throw std::runtime_error("");
//                }
//
//            }
//        }
//
//        setTimePoint("Gmw::generateTriple end");
//        MC_END();
//    }
//

    // The basic protocol where the inputs are not shared:
    // Sender has 
    //   > input x
    //   > rand  a, b
    // Recver has
    //   > input y
    //   > rand  z, d = ac + b
    //
    // Sender sends u = a + x
    // Recver sends w = z + y
    //
    // Sender outputs z1 = wx     = cx + yx
    // Recver outputs z2 = uc + d = cx + ca + ca + b
    //                            = cx + b
    // Observer z1 + z2 = xy
    //
    // The full protocol where the inputs are shared:
    // Sender has 
    //   > input x1, y1
    // Recver has
    //   > input x2, y2
    //
    // The protocols invoke the basic protocol twice.
    //   > Sender inputs (x1, y1)
    //   > Recver inputs (y2, x2)
    // 
    //   > Sender receives (z11, z21)
    //   > Recver receives (z12, z22)
    //
    // The final output is:
    // Sender outputs: z1 = x1y1 + z11 + z21 
    //                    = x1y1 + (x1y2 + r1) + (x2y1 +r2)
    //                    = x1y1 + x1y2 + x2y1 + r
    // Recver outputs: z2 = x2y2 + z12 + z22 
    //                    = x2y2 + r1 + r2
    //                    = x2y2 + r
    coproto::task<> Gmw::roundFunction(coproto::Socket& chl)
    {
        MC_BEGIN(coproto::task<>, this, &chl,
            gates = span<oc::BetaGate>{},
            gate = span<oc::BetaGate>::iterator{},
            dirtyBits = std::vector<u8>{},
            pinnedInputs = std::vector<u8>{},
            in = std::array<span<block>, 2>{},
            out = span<block>{},
            ww = std::vector<block>{},
            print = std::move(coproto::unique_function<void(u64)>{}),
            a = oc::AlignedUnVector<block>{},
            b = oc::AlignedUnVector<block>{},
            c = oc::AlignedUnVector<block>{},
            d = oc::AlignedUnVector<block>{},
            triple = SharedTriple{},
            j = u64{}
        );
        if (!mGen)
            throw RTE_LOC;

        if (mRoundIdx >= mNumRounds)
            throw std::runtime_error("round function called too many times");

        //if (mIdx && mO.mDebug)
        //    oc::lout << "round " << mRoundIdx << std::endl;

        gates = mGates.subspan(0, mCir.mLevelCounts[mRoundIdx]);
        mGates = mGates.subspan(mCir.mLevelCounts[mRoundIdx]);


        if (mO.mDebug)
        {
            dirtyBits.resize(mCir.mWireCount, 0);
            pinnedInputs.resize(mCir.mWireCount, 0);
        }


        a.resize(mWords.cols() * mCir.mLevelAndCounts[mRoundIdx]);
        b.resize(mWords.cols() * mCir.mLevelAndCounts[mRoundIdx]);
        c.resize(mWords.cols() * mCir.mLevelAndCounts[mRoundIdx]);
        d.resize(mWords.cols() * mCir.mLevelAndCounts[mRoundIdx]);

        j = 0;
        while (j != a.size())
        {
            if (triple.size() == 0)
                MC_AWAIT(mGen->get(triple));

            // a  * c  = b  + d
            // a' * c' = b' + d'
            auto min = std::min<u64>(a.size() - j, triple.size() / 128 / 2);

            span<block> aa, bb, cc, dd;
            if (mIdx == 0)
            {
                aa = triple.mMult.subspan(0, min);
                cc = triple.mMult.subspan(min, min);
                bb = triple.mAdd.subspan(0, min);
                dd = triple.mAdd.subspan(min, min);
            }
            else
            {
                cc = triple.mMult.subspan(0, min);
                aa = triple.mMult.subspan(min, min);
                dd = triple.mAdd.subspan(0, min);
                bb = triple.mAdd.subspan(min, min);
            }

            std::copy(aa.begin(), aa.end(), a.begin() + j);
            std::copy(bb.begin(), bb.end(), b.begin() + j);
            std::copy(cc.begin(), cc.end(), c.begin() + j);
            std::copy(dd.begin(), dd.end(), d.begin() + j);

            //if (mIdx == 0)
            //{
            //    std::cout << "0 a " << a[j] << " * c = b " << b[j] << " ^ d" << std::endl;
            //    std::cout << "0 a * c " << c[j] << " = b ^ d " << d[j] << std::endl;
            //}
            //else
            //{
            //    std::cout << "1 a * c " << c[j] << " = b ^ d " << d[j] << std::endl;
            //    std::cout << "1 a " << a[j] << " * c = b " << b[j] << " ^ d" << std::endl;
            //}
            triple.mMult = triple.mMult.subspan(min * 2);
            triple.mAdd = triple.mAdd.subspan(min * 2);
            j += min;

        }


        j = 0;
        for (gate = gates.begin(); gate < gates.end(); ++gate)
        {
            //in = { mWords[gate->mInput[0]], mWords[gate->mInput[1]] };
            //out = mWords[gate->mOutput];

            in[0] = oc::span<oc::block>(mWords.data() + gate->mInput[0] * mWords.cols(), mWords.cols());
            in[1] = oc::span<oc::block>(mWords.data() + gate->mInput[1] * mWords.cols(), mWords.cols());
            out = oc::span<oc::block>(mWords.data() + gate->mOutput * mWords.cols(), mWords.cols());

            //if (mIdx && mO.mDebug)
            //{
            //    oc::RandomOracle ro(16);
            //    ro.Update(mWords.data(), mWords.size());
            //    block h;
            //    ro.Final(h);

            //    oc::lout << "g " << gate->mInput[0] << " " << gate->mInput[1] << " " <<
            //        gateToString(gate->mType) << " " << gate->mOutput << " ~ " << h << std::endl;
            //}
            if (mO.mDebug)
            {
                if (dirtyBits[gate->mInput[0]] ||
                    (dirtyBits[gate->mInput[1]] && gate->mType != oc::GateType::a))
                {
                    throw std::runtime_error("incorrect levelization, input to current gate depends on the output of the current round. " LOCATION);
                }

                if (pinnedInputs[gate->mOutput])
                {
                    throw std::runtime_error("incorrect levelization, overwriting an input which is being used in the current round. " LOCATION);
                }
            }

            if (gate->mType == oc::GateType::a)
            {
                for (u64 i = 0; i < out.size(); ++i)
                    out[i] = in[0][i];
            }
            else if (gate->mType == oc::GateType::na_And ||
                gate->mType == oc::GateType::nb_And ||
                gate->mType == oc::GateType::And ||
                gate->mType == oc::GateType::Nor ||
                gate->mType == oc::GateType::Or)
            {
                MC_AWAIT(multSend(in[0], in[1], chl, gate->mType, a.subspan(j, mWords.cols()), c.subspan(j, mWords.cols())));
                j += mWords.cols();

                if (mO.mDebug)
                {
                    pinnedInputs[gate->mInput[0]] = 1;
                    pinnedInputs[gate->mInput[1]] = 1;
                    dirtyBits[gate->mOutput] = 1;
                }
            }
            else if (
                gate->mType == oc::GateType::Xor ||
                gate->mType == oc::GateType::Nxor)
            {

                for (u64 i = 0; i < out.size(); ++i)
                    out[i] = in[0][i] ^ in[1][i];

                if (gate->mType == oc::GateType::Nxor && mIdx == 0)
                {
                    for (u64 i = 0; i < out.size(); ++i)
                        out[i] = out[i] ^ oc::AllOneBlock;
                }
            }
            else
                throw RTE_LOC;
        }

        j = 0;
        for (gate = gates.begin(); gate < gates.end(); ++gate)
        {
            in[0] = oc::span<oc::block>(mWords.data() + gate->mInput[0] * mWords.cols(), mWords.cols());
            in[1] = oc::span<oc::block>(mWords.data() + gate->mInput[1] * mWords.cols(), mWords.cols());
            out = oc::span<oc::block>(mWords.data() + gate->mOutput * mWords.cols(), mWords.cols());
            //, mWords[gate->mInput[1]]
                //  };

            if (gate->mType == oc::GateType::na_And ||
                gate->mType == oc::GateType::nb_And ||
                gate->mType == oc::GateType::And ||
                gate->mType == oc::GateType::Or ||
                gate->mType == oc::GateType::Nor)
            {
                MC_AWAIT(multRecv(in[0], in[1], out, chl, gate->mType, 
                    b.subspan(j, mWords.cols()), 
                    c.subspan(j, mWords.cols()), 
                    d.subspan(j, mWords.cols())));
                j += mWords.cols();
            }
        }



        if (mO.mDebug)
        {
            print = [&](u64 gIdx) {
                while (
                    mDebugPrintIdx < mN &&
                    mPrint != mCir.mPrints.end() &&
                    mPrint->mGateIdx <= gIdx &&
                    mIdx)
                {
                    auto wireIdx = mPrint->mWire;
                    auto invert = mPrint->mInvert;

                    if (wireIdx != ~u32(0))
                    {
                        oc::BitIterator iter((u8*)mO.mWords[wireIdx].data(), mDebugPrintIdx);
                        auto mem = u64(*iter);
                        std::cout << (u64)(mem ^ (invert ? 1 : 0));
                    }
                    if (mPrint->mMsg.size())
                        std::cout << mPrint->mMsg;

                    ++mPrint;
                }
            };

            for (auto& gate : gates)
            {

                auto gIdx = &gate - mCir.mGates.data();
                print(gIdx);

                for (u64 i = 0; i < mWords.cols(); ++i)
                {
                    auto& a = mO.mWords(gate.mInput[0], i);
                    auto& b = mO.mWords(gate.mInput[1], i);
                    auto& c = mO.mWords(gate.mOutput, i);
                    //if (gate.mOutput == 129)
                    //{
                    //    auto cc=
                    //        (oc::AllOneBlock ^ a) & b;
                    //    std::cout << "~" << a << " & " << b << " -> " << cc << std::endl;
                    //}

                    switch (gate.mType)
                    {
                    case oc::GateType::a:
                        c = a;
                        break;
                    case oc::GateType::And:
                        c = a & b;
                        break;
                    case oc::GateType::Or:
                        c = a | b;
                        break;
                    case oc::GateType::Nor:
                        c = (a | b) ^ oc::AllOneBlock;
                        break;
                    case oc::GateType::nb_And:
                        //oc::lout << "* ~" << a << " & " << b << " -> " << ((oc::AllOneBlock ^ a) & b) << std::endl;
                        c = a & (oc::AllOneBlock ^ b);
                        break;
                    case oc::GateType::na_And:
                        //oc::lout << "* ~" << a << " & " << b << " -> " << ((oc::AllOneBlock ^ a) & b) << std::endl;
                        c = (oc::AllOneBlock ^ a) & b;
                        break;
                    case oc::GateType::Xor:
                        c = a ^ b;
                        break;
                    case oc::GateType::Nxor:
                        c = a ^ b ^ oc::AllOneBlock;
                        break;
                    default:
                        throw RTE_LOC;
                    }
                }
            }

            if (mGates.size() == 0)
            {
                print(mCir.mGates.size());
            }

            MC_AWAIT(chl.send(coproto::copy(mWords)));

            ww.resize(mWords.size());
            MC_AWAIT(chl.recv(ww));

            for (u64 i = 0; i < mWords.size(); ++i)
            {
                auto exp = mO.mWords(i);
                auto act = ww[i] ^ mWords(i);

                if (neq(exp, act))
                {
                    auto row = (i / mWords.cols());
                    auto col = (i % mWords.cols());

                    oc::lout << "p" << mIdx << " mem[" << row << ", " << col << 
                        "] exp: " << exp <<
                        ", act: " << act  <<
                        "\ndiff:" <<(exp^act)<< std::endl;

                    throw RTE_LOC;
                }
            }
        }

        ++mRoundIdx;

        MC_END();
    }

    oc::BitVector view(block v, u64 l = 10)
    {
        return oc::BitVector((u8*)&v, l);
    }

    namespace {
        bool invertA(oc::GateType gt)
        {
            bool invertX;
            switch (gt)
            {
            case osuCrypto::GateType::na_And:
            case osuCrypto::GateType::Or:
            case osuCrypto::GateType::Nor:
                invertX = true;
                break;
            case osuCrypto::GateType::And:
            case osuCrypto::GateType::nb_And:
                invertX = false;
                break;
            default:
                throw RTE_LOC;
            }

            return invertX;
        }
        bool invertB(oc::GateType gt)
        {
            bool invertX;
            switch (gt)
            {
            case osuCrypto::GateType::nb_And:
            case osuCrypto::GateType::Nor:
            case osuCrypto::GateType::Or:
                invertX = true;
                break;
            case osuCrypto::GateType::na_And:
            case osuCrypto::GateType::And:
                invertX = false;
                break;
            default:
                throw RTE_LOC;
            }

            return invertX;
        }
        bool invertC(oc::GateType gt)
        {
            bool invertX;
            switch (gt)
            {
            case osuCrypto::GateType::Or:
                invertX = true;
                break;
            case osuCrypto::GateType::na_And:
            case osuCrypto::GateType::nb_And:
            case osuCrypto::GateType::Nor:
            case osuCrypto::GateType::And:
                invertX = false;
                break;
            default:
                throw RTE_LOC;
            }

            return invertX;
        }
    }

    // The basic protocol where the inputs are not shared:
    // Sender has 
    //   > input x
    //   > rand  a, b
    // Recver has
    //   > input y
    //   > rand  z, d = ac + b
    //
    // Sender sends u = a + x
    // Recver sends w = z + y
    //
    // Sender outputs z1 = wx + b = cx + yx + b
    // Recver outputs z2 = uc + d = (a + x)z + d
    //                            = ac + xc + (ac + b)
    //                            = cx + b
    // Observer z1 + z2 = xy
    coproto::task<> Gmw::multSendP1(span<block> x, coproto::Socket& chl, oc::GateType gt,
        span<block> a)
    {
        MC_BEGIN(coproto::task<>, this, x, &chl, gt, a,
            width = x.size(),
            u = std::vector<block>{}
        );
        //a = mA.subspan(0, width);
        //mA = mA.subspan(width);

        u.reserve(width);
        if (invertA(gt) == false)
        {
            for (u64 i = 0; i < width; ++i)
                u.push_back(a[i] ^ x[i]);
        }
        else
        {
            for (u64 i = 0; i < width; ++i)
                u.push_back(a[i] ^ x[i] ^ oc::AllOneBlock);
        }

        //oc::lout << mIdx << " " << "u = a + x" << std::endl << view(u[0]) << " = " << view(a[0]) << " + " << view(x[0]) << std::endl;

        MC_AWAIT(chl.send(std::move(u)));
        MC_END();
    }

    coproto::task<> Gmw::multSendP2(span<block> y, coproto::Socket& chl, oc::GateType gt,
        span<block> c)
    {

        MC_BEGIN(coproto::task<>, this, y, &chl, gt, c,
            width = y.size(),
            w = std::vector<block>{}
        );
        //c = mC.subspan(0, width);
        //mC = mC.subspan(width);

        w.reserve(width);
        if (invertB(gt) == false)
        {

            for (u64 i = 0; i < width; ++i)
                w.push_back(c[i] ^ y[i]);
        }
        else
        {
            for (u64 i = 0; i < width; ++i)
                w.push_back(c[i] ^ (y[i] ^ oc::AllOneBlock));
        }

        //oc::lout << mIdx << " " << "w = z + y" << std::endl << view(w[0]) << " = " << view(z[0]) << " + " << view(y[0]) << std::endl;

        MC_AWAIT(chl.send(std::move(w)));
        MC_END();
    }

    coproto::task<> Gmw::multRecvP1(span<block> x, span<block> z, coproto::Socket& chl, oc::GateType gt,
        span<block> b)
    {
        MC_BEGIN(coproto::task<>, this, x, z, &chl, gt, b,
            width = x.size(),
            w = std::vector<block>{}
        );
        w.resize(width);
        MC_AWAIT(chl.recv(w));

        //b = mB.subspan(0, width);
        //mB = mB.subspan(width);

        if (invertA(gt) == false)
        {
            for (u64 i = 0; i < width; ++i)
            {
                z[i] = (x[i] & w[i]) ^ b[i];
            }
        }
        else
        {
            for (u64 i = 0; i < width; ++i)
            {
                z[i] = ((oc::AllOneBlock ^ x[i]) & w[i]) ^ b[i];
            }
        }

        MC_END();
        //oc::lout << mIdx << " " << "z1 = x * w" << std::endl << view(z[0]) << " = " << view(x[0]) << " * " << view(w[0]) << std::endl;
    }

    coproto::task<> Gmw::multRecvP2(
        span<block> y,
        span<block> z,
        coproto::Socket& chl,
        span<block> c,
        span<block> d
    )
    {

        MC_BEGIN(coproto::task<>, this, z, &chl, c, d,
            width = y.size(),
            u = std::vector<block>{}
        );
        u.resize(width);
        MC_AWAIT(chl.recv(u));

        //c = mC2.subspan(0, width);
        //mC2 = mC2.subspan(width);
        //d = mD.subspan(0, width);
        //mD = mD.subspan(width);

        for (u64 i = 0; i < width; ++i)
        {
            z[i] = (c[i] & u[i]) ^ d[i];
        }


        MC_END();
        //oc::lout << mIdx << " " << "z2 = z * u + d" << std::endl << view(z[0]) << " = " << view(z[0]) << " * " << view(u[0]) << " + " << view(d[0]) << std::endl;

    }

    coproto::task<> Gmw::multSendP1(span<block> x, span<block> y, coproto::Socket& chl, oc::GateType gt,
        span<block> a,
        span<block> c)
    {
        MC_BEGIN(coproto::task<>, this, x, y, &chl, gt, a, c);
        MC_AWAIT(multSendP1(x, chl, gt, a));
        MC_AWAIT(multSendP2(y, chl, gt, c));
        MC_END();
    }

    coproto::task<> Gmw::multSendP2(span<block> x, span<block> y, coproto::Socket& chl,
        span<block> a,
        span<block> c)
    {
        MC_BEGIN(coproto::task<>, this, x, y, &chl, a, c);

        MC_AWAIT(multSendP2(y, chl, oc::GateType::And, c));
        MC_AWAIT(multSendP1(x, chl, oc::GateType::And, a));

        MC_END();
    }


    coproto::task<> Gmw::multRecvP1(span<block> x, span<block> y, span<block> z, coproto::Socket& chl, oc::GateType gt,
        span<block> b,
        span<block> c,
        span<block> d)
    {
        MC_BEGIN(coproto::task<>, this, x, y, z, &chl, gt, b, c, d,
            zz = std::vector<block>{},
            zz2 = std::vector<block>{},
            xm = block{},
            ym = block{},
            zm = block{}
        );

        zz.resize(z.size());
        zz2.resize(z.size());
        MC_AWAIT(multRecvP1(x, zz, chl, gt, b));
        MC_AWAIT(multRecvP2(y, zz2, chl, c, d));

        xm = invertA(gt) ? oc::AllOneBlock : oc::ZeroBlock;
        ym = invertB(gt) ? oc::AllOneBlock : oc::ZeroBlock;
        zm = invertC(gt) ? oc::AllOneBlock : oc::ZeroBlock;

        for (u64 i = 0; i < zz.size(); i++)
        {
            auto xx = x[i] ^ xm;
            auto yy = y[i] ^ ym;
            z[i] = xx & yy;
            z[i] = z[i] ^ zz[i];
            z[i] = z[i] ^ zz2[i];
            z[i] = z[i] ^ zm;
        }

        MC_END();
    }

    coproto::task<> Gmw::multRecvP2(span<block> x, span<block> y, span<block> z, coproto::Socket& chl,
        span<block> b,
        span<block> c,
        span<block> d)
    {
        MC_BEGIN(coproto::task<>, this, x, y, z, &chl, b, c, d,
            zz = std::vector<block>{},
            zz2 = std::vector<block>{}
        );

        zz.resize(z.size());
        zz2.resize(z.size());

        MC_AWAIT(multRecvP2(y, zz, chl, c, d));
        MC_AWAIT(multRecvP1(x, zz2, chl, oc::GateType::And, b));

        for (u64 i = 0; i < zz.size(); i++)
        {
            z[i] = zz2[i] ^ zz[i] ^ (x[i] & y[i]);
        }

        MC_END();
    }


}