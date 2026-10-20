#include "gate.h"
#include <cassert>
#include <limits>

int main() {
    const std::string session(64, 'a');
    plb::DisplayGate gate(10123, session);
    assert(!gate.attach(0, session, 1, false));
    assert(!gate.attach(10124, session, 1, false));
    assert(!gate.attach(10123, std::string(64, 'b'), 1, false));
    assert(!gate.attach(10123, session, -1, false));
    assert(gate.attach(10123, session, 1, false));
    assert(!gate.attach(10123, session, 1, false));
    assert(gate.attach(10123, session, 3, false));
    assert(!gate.detach(10123, session, 1, false));
    assert(gate.detach(10123, session, 4, false));
    assert(!gate.attach(10123, session, 4, false));
    assert(gate.attach(10123, session, 5, false));
    assert(gate.attach(10123, session, 1, true));
    assert(gate.attach(10123, session, std::numeric_limits<int64_t>::max(), false));
    assert(!gate.attach(10123, session, std::numeric_limits<int64_t>::min(), false));
    gate.close();
    assert(!gate.attach(10123, session, 2, true));
    assert(!gate.detach(10123, session, 2, true));
    plb::DisplayGate invalid(0, session);
    assert(!invalid.authorized(0, session));
    plb::DisplayGate bad_token(10123, std::string(64, 'A'));
    assert(!bad_token.authorized(10123, std::string(64, 'A')));
}
