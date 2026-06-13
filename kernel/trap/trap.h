#pragma once

namespace xv6 {

class spinlock;

// trap.cc
extern uint ticks;
void trapinit();
void trapinithart();
extern spinlock tickslock;
void prepare_return();

} // namespace xv6
