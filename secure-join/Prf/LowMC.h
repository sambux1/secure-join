#pragma once

#include <bitset>
#include <vector>
#include <array>
#include <string>
#include <random>

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include "secure-join/Defines.h"

#include <cryptoTools/Circuit/BetaCircuit.h>
#include <cryptoTools/Crypto/PRNG.h>

namespace secJoin
{
	template<typename block_type>
	u64 rank_of_Matrix(const std::vector<block_type>& matrix);

	template<typename block_type>
	std::vector<block_type> invert_Matrix(const std::vector<block_type> matrix);

	template<
		size_t  numofboxes = 63,
		size_t  blocksize = 256,
		size_t  keysize = 128,
		size_t  rounds = 14
	>
	class LowMC2 {
	public:
		static_assert(numofboxes * 3 <= blocksize, "numofboxes * 3 <= blocksize");

		struct LowMCInputBetaBundle {

			oc::BetaBundle encryptMessage, state, temp, temp2;//, xorMessage;

			std::array<oc::BetaBundle, rounds + 1> roundKeys;
		};

		static size_t getBlockSize() { return blocksize; };
		using block = std::bitset<blocksize>;
		using keyblock = std::bitset<keysize>;

		// Size of the identity part in the Sbox layer
		const u64 identitysize = blocksize - 3 * numofboxes;
		bool mInvertable;

		LowMC2(bool invertable, keyblock k = 0) {
			mInvertable = invertable;
			key = k;
			instantiate_LowMC(invertable);
			keyschedule();
		};

		block encrypt(const block& message)
		{
			//std::cout << "message        " << message << std::endl;
			//std::cout << "key 0          " << roundkeys[0] << std::endl;

			block c = message ^ roundkeys[0];

			//std::cout << "state[0]       " << c << std::endl;

			for (u64 r = 1; r <= rounds; ++r) {
				c = Substitution(c);

				//std::cout << "state[" << r << "].sbox  " << c << std::endl;

				c = MultiplyWithGF2Matrix(LinMatrices[r - 1], c);

				//std::cout << "state[" << r << "].mul   " << c << std::endl;
				c ^= roundconstants[r - 1];

				//std::cout << "state[" << r << "].const " << c << std::endl;

				c ^= roundkeys[r];

				//std::cout << "state[" << r << "]       " << c << std::endl;
			}
			return c;
		}


		block decrypt(const block message)
		{
			if (mInvertable == false)
				throw RTE_LOC;

			block c = message;
			for (u64 r = rounds; r > 0; --r) {
				c ^= roundkeys[r];
				c ^= roundconstants[r - 1];
				c = MultiplyWithGF2Matrix(invLinMatrices[r - 1], c);
				c = invSubstitution(c);
			}
			c ^= roundkeys[0];
			return c;
		}

		void set_key(keyblock k)
		{
			key = k;
			keyschedule();
		}




		void add_lowmc_gates(oc::BetaCircuit& cir, LowMCInputBetaBundle bundle) const
		{
			for (u64 i = 0; i < (u64)blocksize; ++i)
			{
				cir.addGate(bundle.encryptMessage[i], bundle.roundKeys[0][i], oc::GateType::Xor, bundle.state[i]);
			}

			for (u64 r = 0; r < rounds; ++r)
			{
				// SBOX
				for (int i = 0; i < (int)numofboxes; ++i)
				{
					auto& c = bundle.state[i * 3 + 0];
					auto& b = bundle.state[i * 3 + 1];
					auto& a = bundle.state[i * 3 + 2];
					auto& aa = bundle.temp[i * 3 + 0];
					auto& bb = bundle.temp[i * 3 + 1];
					auto& cc = bundle.temp[i * 3 + 2];
					auto& a_b = bundle.temp2[0];

					// a_b = a + b
					cir.addGate(a, b, oc::GateType::Xor, a_b);

					cir.addGate(b, c, oc::GateType::And, aa);

					cir.addGate(a, c, oc::GateType::And, bb);

					cir.addGate(a, b, oc::GateType::And, cc);
					cir.addGate(cc, c, oc::GateType::Xor, cc);

					// a = a + bc
					cir.addGate(a, aa, oc::GateType::Xor, a);
					// b = a + b + ac
					cir.addGate(a_b, bb, oc::GateType::Xor, b);
					// c = a + b + c + ab
					cir.addGate(cc, a_b, oc::GateType::Xor, c);
				}

				// multiply with linear matrix and add const
				auto& matrix = LinMatrices[r];
				for (int i = 0; i < (int)blocksize; ++i)
				{
					auto& t = bundle.temp[i];
					int j = -1, firstIdx, secondIdx;

					auto& row = matrix[i];

					while (row[++j] == 0);
					firstIdx = j;

					while (row[++j] == 0);
					secondIdx = j++;

					cir.addGate(bundle.state[firstIdx], bundle.state[secondIdx], oc::GateType::Xor, t);

					for (; j < (int)blocksize; ++j)
					{
						if (row[j])
						{
							cir.addGate(t, bundle.state[j], oc::GateType::Xor, t);
						}
					}
				}

				for (int i = 0; i < (int)blocksize; ++i)
				{

					if (roundconstants[r][i])
					{
						cir.addInvert(bundle.temp[i]);
					}
				}

				// add key
				for (int i = 0; i < (int)blocksize; ++i)
				{
					cir.addGate(bundle.temp[i], bundle.roundKeys[r + 1][i], oc::GateType::Xor, bundle.state[i]);
				}


			}
		}

		// LowMC circuit. 
		// input[0] = message
		// input[1] = key[0]
		// ...
		// input[numRounds] = key[numRounds-1]
		// 
		// output[0] = LowMc(key, message)
		void to_enc_circuit(oc::BetaCircuit& cir) const
		{
			LowMCInputBetaBundle bundle;
			bundle.encryptMessage.mWires.resize(blocksize);
			bundle.state.mWires.resize(blocksize);
			bundle.temp.mWires.resize(blocksize);
			bundle.temp2.mWires.resize(1);

			cir.addInputBundle(bundle.encryptMessage);

			for (auto& key : bundle.roundKeys)
			{
				key.mWires.resize(blocksize);
				cir.addInputBundle(key);
			}


			cir.addOutputBundle(bundle.state);

			cir.addTempWireBundle(bundle.temp);
			cir.addTempWireBundle(bundle.temp2);

			add_lowmc_gates(cir, bundle);

		}

		void print_matrices()
		{
			std::cout << "LowMC2 matrices and constants" << std::endl;
			std::cout << "============================" << std::endl;
			std::cout << "Block size: " << blocksize << std::endl;
			std::cout << "Key size: " << keysize << std::endl;
			std::cout << "Rounds: " << rounds << std::endl;
			std::cout << std::endl;

			std::cout << "Linear layer matrices" << std::endl;
			std::cout << "---------------------" << std::endl;
			for (u64 r = 1; r <= rounds; ++r) {
				std::cout << "Linear layer " << r << ":" << std::endl;
				for (auto row : LinMatrices[r - 1]) {
					std::cout << "[";
					for (u64 i = 0; i < blocksize; ++i) {
						std::cout << row[i];
						if (i != blocksize - 1) {
							std::cout << ", ";
						}
					}
					std::cout << "]" << std::endl;
				}
				std::cout << std::endl;
			}

			std::cout << "Round constants" << std::endl;
			std::cout << "---------------------" << std::endl;
			for (u64 r = 1; r <= rounds; ++r) {
				std::cout << "Round constant " << r << ":" << std::endl;
				std::cout << "[";
				for (u64 i = 0; i < blocksize; ++i) {
					std::cout << roundconstants[r - 1][i];
					if (i != blocksize - 1) {
						std::cout << ", ";
					}
				}
				std::cout << "]" << std::endl;
				std::cout << std::endl;
			}

			std::cout << "Round key matrices" << std::endl;
			std::cout << "---------------------" << std::endl;
			for (u64 r = 0; r <= rounds; ++r) {
				std::cout << "Round key matrix " << r << ":" << std::endl;
				for (auto row : KeyMatrices[r]) {
					std::cout << "[";
					for (u64 i = 0; i < keysize; ++i) {
						std::cout << row[i];
						if (i != keysize - 1) {
							std::cout << ", ";
						}
					}
					std::cout << "]" << std::endl;
				}
				if (r != rounds) {
					std::cout << std::endl;
				}
			}
		}

		//private:
			// LowMC2 private data members //

			// The Sbox and its inverse    
		const std::array<u8, 8> Sbox =
		{ { 0x00, 0x01, 0x03, 0x06, 0x07, 0x04, 0x05, 0x02 } };
		const std::array<u8, 8> invSbox =
		{ { 0x00, 0x01, 0x07, 0x02, 0x05, 0x06, 0x03, 0x04 } };

		// Stores the binary matrices for each round
		std::vector<std::vector<block>> LinMatrices;
		// Stores the inverses of LinMatrices
		std::vector<std::vector<block>> invLinMatrices;
		// Stores the round constants
		std::vector<block> roundconstants;
		//Stores the master key
		keyblock key = 0;
		// Stores the matrices that generate the round keys
		std::vector<std::vector<keyblock>> KeyMatrices;
		// Stores the round keys
		std::vector<block> roundkeys;

		std::bitset<80> state;
		// LowMC2 private functions //

		// The substitution layer
		block Substitution(const block message)
		{
			block temp = 0;
			//Get the identity part of the message
			temp ^= (message >> 3 * numofboxes);
			//Get the rest through the Sboxes
			for (u64 i = 1; i <= numofboxes; ++i) {
				temp <<= 3;
				temp ^= Sbox[((message >> 3 * (numofboxes - i))
					& block(0x7)).to_ulong()];
			}
			return temp;
		}

		// The inverse substitution layer
		block invSubstitution(const block message)
		{
			block temp = 0;
			//Get the identity part of the message
			temp ^= (message >> 3 * numofboxes);
			//Get the rest through the invSboxes
			for (u64 i = 1; i <= numofboxes; ++i) {
				temp <<= 3;
				temp ^= invSbox[((message >> 3 * (numofboxes - i))
					& block(0x7)).to_ulong()];
			}
			return temp;
		}

		// For the linear layer
		template<typename block_type>
		block MultiplyWithGF2Matrix(const std::vector<block_type> matrix, const block_type message)
		{
			block temp = 0;
			for (u64 i = 0; i < blocksize; ++i) {
				temp[i] = (message & matrix[i]).count() & 1;
			}
			return temp;
		}

		//Creates the round keys from the master key
		void keyschedule()
		{
			roundkeys.clear();
			for (u64 r = 0; r <= rounds; ++r) {
				roundkeys.push_back(MultiplyWithGF2Matrix(KeyMatrices[r], key));
			}
			return;
		}


		template<typename block_type>
		std::vector<block_type> loadMatrix(std::istream& in)
		{
			std::vector<block_type> mat(blocksize);
			char c;

			for (int i = 0; i < (int)mat.size(); ++i)
			{
				for (int j = 0; j < (int)mat[i].size(); ++j)
				{
					in.read(&c, 1);
					if (c == '0')
						mat[i][j] = 0;
					else if (c == '1')
						mat[i][j] = 1;
					else
						throw std::runtime_error(LOCATION);
				}

				in.read(&c, 1);
				if (c != '\n')
					throw std::runtime_error(LOCATION);
			}
			return mat;
		}

		template<typename block_type>
		void writeMatrix(std::ostream& out, const std::vector<block_type>& mat)
		{
			char c;
			for (int i = 0; i < mat.size(); ++i)
			{
				for (int j = 0; j < mat[i].size(); ++j)
				{
					c = '0' + mat[i][j];
					out.write(&c, 1);
				}

				c = '\n';
				out.write(&c, 1);
			}
		}



		//Fills the matrices and roundconstants with pseudorandom bits 
		void instantiate_LowMC(bool invertable)
		{
			PRNG prng(oc::ZeroBlock);

			// Create LinMatrices and invLinMatrices
			LinMatrices.clear();
			invLinMatrices.clear();
			for (u64 r = 0; r < rounds; ++r) {

				std::vector<block> mat;

				if (invertable == false)
				{
					for (u64 i = 0; i < blocksize; ++i) {
						mat.push_back(getrandblock(prng));
					}
					LinMatrices.push_back(mat);
				}
				else
				{
					std::string fileName("./linMtx_" + std::to_string(r) + ".txt");
					std::ifstream in;
					in.open(fileName, std::ios::in);

					if (in.is_open() == false)
					{

						// Create matrix
						// Fill matrix with random bits
						do {
							mat.clear();
							for (u64 i = 0; i < blocksize; ++i) {
								mat.push_back(getrandblock(prng));
							}
							// Repeat if matrix is not invertible
						} while (rank_of_Matrix(mat) != blocksize);

						//std::ofstream out;
						//out.open(fileName, std::ios::out | std::ios::binary | std::ios::trunc);
						//writeMatrix(out, mat);
					}
					else
					{
						mat = loadMatrix<block>(in);

						if (rank_of_Matrix(mat) != blocksize)
							throw std::runtime_error(LOCATION);
					}
					LinMatrices.push_back(mat);
					invLinMatrices.push_back(invert_Matrix(LinMatrices.back()));

					in.close();
				}
			}



			// Create roundconstants
			roundconstants.clear();
			for (u64 r = 0; r < rounds; ++r) {
				roundconstants.push_back(getrandblock(prng));
			}

			// Create KeyMatrices
			KeyMatrices.clear();
			for (u64 r = 0; r <= rounds; ++r) {
				// Create matrix
				std::vector<keyblock> mat;

				if (invertable == false)
				{
					for (u64 i = 0; i < blocksize; ++i) {
						mat.push_back(getrandkeyblock(prng));
					}
				}
				else
				{
					std::string fileName("./keyMtx_" + std::to_string(r) + ".txt");
					std::ifstream in;
					in.open(fileName, std::ios::in);

					if (in.is_open() == false)
					{

						// Fill matrix with random bits
						do {
							mat.clear();
							for (u64 i = 0; i < blocksize; ++i) {
								mat.push_back(getrandkeyblock(prng));
							}
							// Repeat if matrix is not of maximal rank
						} while (rank_of_Matrix(mat) < std::min(blocksize, keysize));

						//std::ofstream out;
						//out.open(fileName, std::ios::out | std::ios::binary | std::ios::trunc);
						//writeMatrix(out, mat);
					}
					else
					{
						mat = loadMatrix<keyblock>(in);

						if (rank_of_Matrix(mat) < std::min(blocksize, keysize))
							throw std::runtime_error(LOCATION);
					}

					in.close();
				}
				KeyMatrices.push_back(mat);
			}

			return;
		}


		// Random bits functions //
		block getrandblock(PRNG& prng)
		{
			block tmp = 0;
			for (u64 i = 0; i < blocksize; ++i) tmp[i] = prng.get<bool>();
			return tmp;
			//return prng.get();
		}


		keyblock getrandkeyblock(PRNG& prng)
		{
			keyblock tmp = 0;
			for (u64 i = 0; i < keysize; ++i) tmp[i] = prng.get<bool>();
			return tmp;
		}

		//bool  getrandbit()
		//{
		//    //static std::mt19937 gen(0); //Standard mersenne_twister_engine seeded with zero
		//    //std::uniform_int_distribution<> dis(0, 1);
		//    //return dis(gen);

		//     //Keeps the 80 bit LSFR state
		//    bool tmp = 0;
		//    //If state has not been initialized yet
		//    if (state.none()) {
		//        state.set(); //Initialize with all bits set
		//                     //Throw the first 160 bits away
		//        for (u64 i = 0; i < 160; ++i) {
		//            //Update the state
		//            tmp = state[0] ^ state[13] ^ state[23]
		//                ^ state[38] ^ state[51] ^ state[62];
		//            state >>= 1;
		//            state[79] = tmp;
		//        }
		//    }
		//    //choice records whether the first bit is 1 or 0.
		//    //The second bit is produced if the first bit is 1.
		//    bool choice = false;
		//    do {
		//        //Update the state
		//        tmp = state[0] ^ state[13] ^ state[23]
		//            ^ state[38] ^ state[51] ^ state[62];
		//        state >>= 1;
		//        state[79] = tmp;
		//        choice = tmp;
		//        tmp = state[0] ^ state[13] ^ state[23]
		//            ^ state[38] ^ state[51] ^ state[62];
		//        state >>= 1;
		//        state[79] = tmp;
		//    } while (!choice);
		//    return tmp;
		//}

	};


	// Binary matrix functions //   

	template<typename block_type>
	std::vector<block_type> invert_Matrix(const std::vector<block_type> matrix)
	{
		std::vector<block_type> mat = matrix;
		auto blocksize = mat[0].size();

		std::vector<block_type> invmat(blocksize, 0); //To hold the inverted matrix
		for (u64 i = 0; i < blocksize; ++i) {
			invmat[i][i] = 1;
		}

		u64 size = mat[0].size();
		//Transform to upper triangular matrix
		u64 row = 0;
		for (u64 col = 0; col < size; ++col) {
			if (!mat[row][col]) {
				u64 r = row + 1;
				while (r < mat.size() && !mat[r][col]) {
					++r;
				}
				if (r >= mat.size()) {
					continue;
				}
				else {
					auto temp = mat[row];
					mat[row] = mat[r];
					mat[r] = temp;
					temp = invmat[row];
					invmat[row] = invmat[r];
					invmat[r] = temp;
				}
			}
			for (u64 i = row + 1; i < mat.size(); ++i) {
				if (mat[i][col]) {
					mat[i] ^= mat[row];
					invmat[i] ^= invmat[row];
				}
			}
			++row;
		}

		//Transform to identity matrix
		for (u64 col = size; col > 0; --col) {
			for (u64 r = 0; r < col - 1; ++r) {
				if (mat[r][col - 1]) {
					mat[r] ^= mat[col - 1];
					invmat[r] ^= invmat[col - 1];
				}
			}
		}

		return invmat;
	}

	template<typename block_type>
	u64 rank_of_Matrix(const std::vector<block_type>& matrix)
	{
		std::vector<block_type> mat = matrix;

		auto size = mat[0].size();
		//Transform to upper triangular matrix
		u64 row = 0;
		for (u64 col = 1; col <= size; ++col) {
			if (!mat[row][size - col]) {
				u64 r = row;
				while (r < mat.size() && !mat[r][size - col]) {
					++r;
				}
				if (r >= mat.size()) {
					continue;
				}
				else {
					auto temp = mat[row];
					mat[row] = mat[r];
					mat[r] = temp;
				}
			}
			for (u64 i = row + 1; i < mat.size(); ++i) {
				if (mat[i][size - col]) mat[i] ^= mat[row];
			}
			++row;
			if (row == size) break;
		}
		return row;
	}

}