#ifndef PAQ8GEN_ARITHMETICENCODER_HPP
#define PAQ8GEN_ARITHMETICENCODER_HPP

#include "file/FileDisk.hpp"

class ArithmeticEncoder {
public:
  ArithmeticEncoder(File* archive);
  constexpr static int PRECISION = 28; //28 bit precision
  uint32_t x1, x2; /**< Range, initially [0, 1), scaled by 2^32 */
  uint32_t x; /**< Decompress mode: last 4 input bytes of archive */
  uint32_t pending_bits;
  uint8_t B;  /**< a byte from the archive */
  uint32_t bits_in_B;
  File *archive; /**< Compressed data file */
  void prefetch();
  void flush();
  void encodeBit(uint32_t p, const int bit);
  int decodeBit(uint32_t p);
private:
  int bit_read();
  void bit_write(const int bit);
  void bit_write_with_pending(const int bit);
};

#endif //PAQ8GEN_ARITHMETICENCODER_HPP
