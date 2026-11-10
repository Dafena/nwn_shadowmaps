#include "../drawing_probe.h"
#include "../drawing_input_journal.h"
#include <cassert>
#include <string>
int main() {
    const auto prefix=std::string("EOMDRAW1|")+std::string(64,'a');
    drawing_probe::Message m;
    assert(drawing_probe::parse((prefix+"|D|1|20.5|30|512|300").c_str(),m));
    assert(m.kind=='D' && m.x==20.5f && m.w==512);
    assert(drawing_probe::parse((prefix+"|B|2|10|20|200|100").c_str(),m));
    for(const auto& suffix:{"|D|1|nan|2|3|4","|D|1|1|2|-3|4","|X|1|1|2|3|4",
                           "|D|1|1|2|3|4|extra","|D|1|1|2|999999|4",
                           "|D|0|1|2|3|4","|D|-1|1|2|3|4","|D|4097|1|2|3|4"})
        assert(!drawing_probe::parse((prefix+suffix).c_str(),m));
    assert(!drawing_probe::parse("EOMDRAW1|short|D|1|1|2|3|4",m));

    drawing_probe::InputJournal journal;
    for(unsigned i=0;i<100000;++i)assert(!journal.record(0,i*10000ULL));
    assert(journal.movements==0 && journal.controls==0);
    assert(journal.record(1,1000000000));
    assert(!journal.record(1,1000000001)); // duplicate polling/event adapter down
    assert(!journal.record(0,1000000002));
    assert(journal.record(0,1000010000));
    for(unsigned i=0;i<10000;++i)journal.record(0,1000020000ULL+i*10000ULL);
    assert(journal.movements==journal.movement_limit);
    assert(journal.record(2,2000000000)); // release survives exhausted moves
    assert(!journal.held && !journal.record(2,2000000001));
    assert(journal.record(1,2000000002));
    assert(journal.record(3,2000000003) && !journal.held); // blur cancels a drag
    assert(!journal.record(0,2000010000));
    journal={};
    assert(journal.record(1,0) && journal.movements==0 && journal.controls==1);
}
