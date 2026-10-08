#include "hwmath/xboard_irq_lines.hpp"
#include <cassert>
#include <iostream>
int main(){
 XBoardMainIrqLines lines;
 lines.update(true,false,100);assert(lines.level()==0);
 lines.deliver(99);assert(lines.level()==0);lines.deliver(100);assert(lines.level()==2);
 lines.update(false,true,200);assert(lines.level()==2);
 lines.deliver(200);assert(lines.level()==4);
 lines.update(true,true,300);lines.deliver(300);assert(lines.level()==6);
 lines.update(false,true,400);lines.deliver(400);assert(lines.level()==4);
 lines.update(false,false,500);lines.update(true,false,600);
 // Later line changes join the first synchronization's queue, just as
 // empty_event_queue drains the queued MAME states at its first deadline.
 lines.deliver(499);assert(lines.level()==4);
 lines.deliver(500);assert(lines.level()==2);
 lines.update(false,false,700);lines.deliver(700);assert(lines.level()==0);
 std::cout<<"Main IRQ synchronized lines: 11 assertions passed\n";
}
