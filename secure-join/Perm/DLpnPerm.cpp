#include "DLpnPerm.h"

namespace secJoin
{

    void DLpnPerm::setupDlpnSender(oc::block &key, std::vector<oc::block> &rk)
    {
        mSender.setKey(key);
        mSender.setKeyOts(rk);
    }

    void DLpnPerm::setupDlpnReceiver(std::vector<std::array<oc::block, 2>> &sk)
    {
        mRecver.setKeyOts(sk);
    }

    macoro::task<> DLpnPerm::setupDlpnSender(OleGenerator &ole)
    {
        return mSender.genKeyOts(ole);
    }

    macoro::task<> DLpnPerm::setupDlpnReceiver(OleGenerator &ole)
    {
        return mRecver.genKeyOts(ole);
    }

    void xorShare(oc::MatrixView<const u8> v1,
                  oc::MatrixView<const oc::u8> v2,
                  oc::MatrixView<oc::u8> &s)
    {
        // Checking the dimensions
        if (v1.rows() != v2.rows())
            throw RTE_LOC;
        if (v1.cols() != v2.cols())
            throw RTE_LOC;
        for (oc::u64 i = 0; i < v1.size(); ++i)
            s(i) = v1(i) ^ v2(i);
    }

    // DLpn Receiver calls this setup
    macoro::task<> DLpnPerm::setup(
        const Perm &pi,
        u64 bytesPerRow,
        oc::PRNG &prng,
        coproto::Socket &chl,
        OleGenerator &ole,
        bool invPerm)
    {
        MC_BEGIN(macoro::task<>, &pi, &chl, &prng, &ole, bytesPerRow, this, invPerm,
                 aesPlaintext = oc::Matrix<oc::block>(),
                 aesCipher = oc::Matrix<oc::block>(),
                 dlpnCipher = oc::Matrix<oc::block>(),
                 blocksPerRow = u64(),
                 totElements = u64(),
                 aes = oc::AES(),
                 key = oc::block());


        totElements = pi.mPerm.size();
        blocksPerRow = oc::divCeil(bytesPerRow, sizeof(oc::block));

        key = prng.get();
        aes.setKey(key);
        MC_AWAIT(chl.send(std::move(key)));
        // if(mRecver.hasKeyOts() == false)
        //     MC_AWAIT(mRecver.genKeyOts(ole));

        // Encryption starts here
        aesPlaintext.resize(totElements, blocksPerRow);
        aesCipher.resize(totElements, blocksPerRow);
        dlpnCipher.resize(totElements, blocksPerRow);
        for (u64 i = 0; i < totElements; i++)
        {
            for (u64 j = 0; j < blocksPerRow; j++)
            {
                if (!invPerm)
                {
                    auto srcIdx = pi[i] * blocksPerRow + j;
                    aesPlaintext(i, j) = oc::block(0, srcIdx);
                }
                else
                {
                    auto srcIdx = i * blocksPerRow + j;
                    aesPlaintext(pi[i], j) = oc::block(0, srcIdx);
                }
            }
        }
        aes.ecbEncBlocks(aesPlaintext, aesCipher);

        MC_AWAIT(mRecver.evaluate(aesCipher, dlpnCipher, chl, prng, ole));

        mDelta.resize(totElements, bytesPerRow);
        for (u64 i = 0; i < totElements; i++)
            memcpyMin(mDelta[i], dlpnCipher[i]);

        MC_END();
    }

    // DLpn Sender calls this setup
    macoro::task<> DLpnPerm::setup(
        u64 totElements,
        u64 bytesPerRow,
        oc::PRNG &prng,
        coproto::Socket &chl,
        OleGenerator &ole)
    {

        MC_BEGIN(macoro::task<>, &chl, &prng, &ole, totElements, bytesPerRow, this,
                 aesPlaintext = oc::Matrix<oc::block>(),
                 aesCipher = oc::Matrix<oc::block>(),
                 preProsdlpnCipher = oc::Matrix<oc::block>(),
                 dlpnCipher = oc::Matrix<oc::block>(),
                 blocksPerRow = u64(),
                 aes = oc::AES(),
                 key = oc::block());


        blocksPerRow = oc::divCeil(bytesPerRow, sizeof(oc::block));

        // Calculating a from the ppt
        aesPlaintext.resize(totElements, blocksPerRow);
        aesCipher.resize(totElements, blocksPerRow);
        dlpnCipher.resize(totElements, blocksPerRow);
        preProsdlpnCipher.resize(totElements, blocksPerRow);

        for (u64 i = 0; i < aesPlaintext.size(); i++)
            aesPlaintext(i) = oc::block(0, i);


        MC_AWAIT(chl.recv(key));
        aes.setKey(key);
        aes.ecbEncBlocks(aesPlaintext, aesCipher);

        MC_AWAIT(mSender.evaluate(dlpnCipher, chl, prng, ole));

        for (u64 i = 0; i < aesCipher.rows(); i++)
        {
            for (u64 j = 0; j < aesCipher.cols(); j++)
            {
                preProsdlpnCipher(i, j) = mSender.mPrf.eval(aesCipher(i, j));
            }
        }

        // Placing a in sout[0]
        mA.resize(totElements, bytesPerRow);
        for (u64 i = 0; i < totElements; i++)
            memcpyMin(mA[i], preProsdlpnCipher[i]);

        // Placing [y] in sout[1]
        mB.resize(totElements, bytesPerRow);

        for (u64 i = 0; i < totElements; i++)
            memcpyMin(mB[i], dlpnCipher[i]);
        MC_END();
    }
    
    template <>
    macoro::task<> DLpnPerm::apply<u8>(
        const Perm &pi,
        u64 bytesPerRow,
        oc::PRNG &prng,
        coproto::Socket &chl,
        OleGenerator &ole,
        oc::MatrixView<u8> sout,
        bool invPerm)
    {
        MC_BEGIN(macoro::task<>, &pi, &chl, &prng, &ole, bytesPerRow, this, sout, invPerm,
                 xEncrypted = oc::Matrix<u8>(),
                 xPermuted = oc::Matrix<u8>(),
                 totElements = u64());

        if(hasSetup() == false)
            MC_AWAIT(setup(pi, bytesPerRow, prng, chl, ole, invPerm));
        //MC_AWAIT(apply(pi, sout, bytesPerRow, chl, invPerm));

        totElements = pi.mPerm.size();
        xPermuted.resize(totElements, bytesPerRow);
        xEncrypted.resize(totElements, bytesPerRow);

        MC_AWAIT(chl.recv(xEncrypted));

        for (u64 i = 0; i < totElements; ++i)
        {
            if (!invPerm)
            {
                memcpy(xPermuted[i], xEncrypted[pi[i]]);
            }
            else
            {
                memcpy(xPermuted[pi[i]], xEncrypted[i]);
            }
        }

        xorShare(mDelta, xPermuted, sout);
        mDelta.resize(0,0);

        MC_END();
    }

    // If DLPN receiver only wants to call apply
    // when it also has inputs
    // this will internally call setup for it
    template <>
    macoro::task<> DLpnPerm::apply<u8>(
        const Perm &pi,
        oc::PRNG &prng,
        coproto::Socket &chl,
        OleGenerator &ole,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        bool invPerm)
    {
        MC_BEGIN(macoro::task<>, &pi, &chl, &prng, &ole, this, sout, invPerm, in,
                 xPermuted = oc::Matrix<u8>(),
                 soutPerm = oc::Matrix<u8>(),
                 bytesPerRow = u64());

        bytesPerRow = in.cols();

        xPermuted.resize(in.rows(), in.cols());
        soutPerm.resize(sout.rows(), sout.cols());

        MC_AWAIT(apply(pi, bytesPerRow, prng, chl, ole, soutPerm, invPerm));
        // MC_AWAIT(setup(pi, bytesPerRow, prng, chl, ole, invPerm));
        // MC_AWAIT(apply(pi, soutPerm, bytesPerRow, chl, invPerm));

        pi.apply<u8>(in, xPermuted, invPerm);
        xorShare(xPermuted, soutPerm, sout);

        MC_END();
    }

    // If DLPN sender only wants to call apply
    // this will internally call setup for it
    template <>
    macoro::task<> DLpnPerm::apply<u8>(
        oc::PRNG &prng,
        coproto::Socket &chl,
        OleGenerator &ole,
        oc::MatrixView<const u8> input,
        oc::MatrixView<u8> sout)
    {
        MC_BEGIN(macoro::task<>, &chl, &prng, &ole, this, input, sout,
                 totElements = u64(),
                 bytesPerRow = u64(),
                 xEncrypted = oc::Matrix<u8>());

        totElements = input.rows();
        bytesPerRow = input.cols();
        if(hasSetup() == false)
            MC_AWAIT(setup(totElements, bytesPerRow, prng, chl, ole));

        // MC_AWAIT(apply(input, sout, chl));
        //totElements = input.rows();
        xEncrypted.resize(input.rows(), input.cols());

        xorShare(mA, input, xEncrypted);

        MC_AWAIT(chl.send(std::move(xEncrypted)));

        for (u64 i = 0; i < totElements; ++i)
            memcpy(sout[i], mB[i]);

        mA.resize(0,0);
        mB.resize(0,0);
        MC_END();
    }
}