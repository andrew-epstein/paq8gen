#ifndef PAQ8GEN_SIMDMIXER_HPP
#define PAQ8GEN_SIMDMIXER_HPP

#include "UpdateBroadcaster.hpp"
#include "Ilog.hpp"
#include "Mixer.hpp"
#include "Squash.hpp"

#ifndef CHALLENGE
template<SIMD simd>
#endif
class SIMDMixer : public Mixer {
private:
    SIMDMixer *mp; /**< points to a Mixer to combine results */

    /**
     * Define padding requirements.
     */
#ifndef CHALLENGE
    [[nodiscard]] constexpr inline auto simdWidth() const -> int {
      if( simd == SIMD_AVX2 ) {
        return 32 / sizeof(short); // 256 bit (32 byte) data size
      }
      else if( simd == SIMD_SSE2 || simd == SIMD_SSSE3 || simd == SIMD_NEON ) {
        return 16 / sizeof(short); // 128 bit (16 byte) data size
      }
      else if( simd == SIMD_NONE ) {
        return 4 / sizeof(short); // Processes 2 shorts at once -> width is 4 bytes
      }
      assert(false);
    }
#endif

public:
#ifndef CHALLENGE
    SIMDMixer(const Shared* const sh, const int n, const int m, const int s) : Mixer(sh, ((n + (simdWidth() - 1)) & -(simdWidth())), m, s) {
      assert((this->n & (simdWidth() - 1)) == 0);
      assert(this->m > 0);
      assert(this->s > 0);
#else
    SIMDMixer(const Shared* const sh, const int n, const int m, const int s) : Mixer(sh, ((n + 15) & -15), m, s) {
#endif

#ifndef CHALLENGE
      mp = (s > 1) ? new SIMDMixer<simd>(sh, s, 1, 1) : nullptr;
#else
      mp = (s > 1) ? new SIMDMixer(sh, s, 1, 1) : nullptr;
#endif
    }

    ~SIMDMixer() override {
      delete mp;
    }

    void setScaleFactor(const int sf0, const int sf1) override {
      scaleFactor = sf0;
      if( mp ) {
        mp->setScaleFactor(sf1, 0);
      }
    }

    void promote(int x) override {
      if (mp != nullptr)
        mp->add(x);
    }

    /**
     * Adjust weights to minimize coding cost of last prediction.
     * Trains the network where the expected output is the last bit (in the shared variable y).
     */
    void update() override {
      INJECT_SHARED_y
      const int target = y << 12;
      if( nx > 0 ) {
        for( uint64_t i = 0; i < numContexts; ++i ) {
          if (cxt[i] != UINT32_MAX) {
            const int err = target - pr[i];
            int rate = rates[i];
            if (mp == nullptr) {
              if (rate > MIN_LEARNING_RATE_S1) rate--;
              rates[i] = rate;
            }
            else {
              if (rate > MIN_LEARNING_RATE_SN) rate--;
              rates[i] = rate;
            }
#ifndef CHALLENGE
            if (simd == SIMD_NONE) {
              trainSimdNone(&tx[0], &wx[cxt[i] * n], nx, (err * rate) >> 16);
            }
            else if (simd == SIMD_SSE2 || simd == SIMD_SSSE3) {
              trainSimdSse2(&tx[0], &wx[cxt[i] * n], nx, (err * rate) >> 16);
            }
            else if (simd == SIMD_AVX2) {
              trainSimdAvx2(&tx[0], &wx[cxt[i] * n], nx, (err * rate) >> 16);
            }
            else if (simd == SIMD_NEON) {
              trainSimdNeon(&tx[0], &wx[cxt[i] * n], nx, (err * rate) >> 16);
            }
#else
            trainSimdAvx2(&tx[0], &wx[cxt[i] * n], nx, (err * rate) >> 16);
#endif
          }
        }
      }
      reset();
    }

    /**
     * Predict next bit
     * @return prediction
     */
    auto p() -> int override {
      shared->GetUpdateBroadcaster()->subscribe(this);
      assert(scaleFactor > 0);
      //if(mp)printf("nx: %d, numContexts: %d, base: %d\n",nx, numContexts, base); //for debugging: how many inputs do we have?
#ifndef CHALLENGE
      while( nx & (simdWidth() - 1)) {
#else
        while( nx & 15 ) {
#endif
        tx[nx++] = 0; // pad
      }
      if( mp ) { // combine outputs
        for( uint64_t i = 0; i < numContexts; ++i ) {
          int dp = 0;
          if (cxt[i] != UINT32_MAX) { // valid mixer context (not to skip)
#ifndef CHALLENGE
            if (simd == SIMD_NONE) {
              dp = dotProductSimdNone(&tx[0], &wx[cxt[i] * n], nx);
            }
            else if (simd == SIMD_SSE2 || simd == SIMD_SSSE3) {
              dp = dotProductSimdSse2(&tx[0], &wx[cxt[i] * n], nx);
            }
            else if (simd == SIMD_AVX2) {
              dp = dotProductSimdAvx2(&tx[0], &wx[cxt[i] * n], nx);
            }
            else if (simd == SIMD_NEON) {
              dp = dotProductSimdNeon(&tx[0], &wx[cxt[i] * n], nx);
            }
#else
            dp = dotProductSimdAvx2(&tx[0], &wx[cxt[i] * n], nx);
#endif
            dp = (dp * scaleFactor) >> 16;
            if (dp < -2047) {
              dp = -2047;
            }
            else if (dp > 2047) {
              dp = 2047;
            }
          }
          mp->add(dp);
          pr[i] = squash(dp);
        }
        mp->set(0, 1);
        return mp->p();
      } // s=1 context
      int dp;
#ifndef CHALLENGE
      if( simd == SIMD_NONE ) {
        dp = dotProductSimdNone(&tx[0], &wx[cxt[0] * n], nx);
      }
      else if( simd == SIMD_SSE2 || simd == SIMD_SSSE3 ) {
        dp = dotProductSimdSse2(&tx[0], &wx[cxt[0] * n], nx);
      }
      else if( simd == SIMD_AVX2 ) {
        dp = dotProductSimdAvx2(&tx[0], &wx[cxt[0] * n], nx);
      }
      else if (simd == SIMD_NEON) {
        dp = dotProductSimdNeon(&tx[0], &wx[cxt[0] * n], nx);
      }
#else
      dp = dotProductSimdAvx2(&tx[0], &wx[cxt[0] * n], nx);
#endif
      dp = (dp * scaleFactor) >> 16;
      return pr[0] = squash(dp);
    }
};

#endif //PAQ8GEN_SIMDMIXER_HPP
