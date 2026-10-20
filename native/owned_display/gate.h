/* MIT. Caller/session/generation policy, independent of Android and GPU APIs. */
#ifndef PLB_OWNED_DISPLAY_GATE_H
#define PLB_OWNED_DISPLAY_GATE_H
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace plb {
inline bool valid_session(const std::string &value) {
    if (value.size() != 64)
        return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
class DisplayGate {
  public:
    DisplayGate(int uid, std::string session) : uid_(uid), session_(std::move(session)) {
        active_ = uid >= 10000 && uid <= 19999 && valid_session(session_);
    }
    bool authorized(int uid, const std::string &session) const {
        if (!active_ || uid != uid_ || session.size() != session_.size())
            return false;
        unsigned difference = 0;
        for (size_t i = 0; i < session.size(); i++)
            difference |= static_cast<unsigned char>(session[i] ^ session_[i]);
        return difference == 0;
    }
    bool attach(int uid, const std::string &session, int64_t generation, bool cursor) {
        auto &slot = slots_[cursor ? 1 : 0];
        if (!authorized(uid, session) || generation <= slot.generation || generation <= 0)
            return false;
        slot.generation = generation;
        slot.attached = true;
        return true;
    }
    bool detach(int uid, const std::string &session, int64_t generation, bool cursor) {
        auto &slot = slots_[cursor ? 1 : 0];
        if (!authorized(uid, session) || generation <= 0 || generation < slot.generation)
            return false;
        // A detach can overtake its attach; seal that generation against late work.
        slot.generation = generation;
        slot.attached = false;
        return true;
    }
    void close() {
        active_ = false;
    }

  private:
    struct Slot {
        int64_t generation = 0;
        bool attached = false;
    };
    int uid_;
    std::string session_;
    bool active_ = false;
    std::array<Slot, 2> slots_{};
};
} // namespace plb
#endif
