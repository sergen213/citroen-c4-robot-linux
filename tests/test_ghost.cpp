#include "game/Ghost.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace {
using namespace c4::game;
using c4::physics::Var;
using c4::physics::Vec3;
using c4::physics::v;
template<class Fn> void rejects(Fn fn) {
    bool rejected=false;
    try { fn(); } catch (const std::invalid_argument&) { rejected=true; }
    assert(rejected);
}
template<class Fn> void save_rejects(Fn fn) {
    bool rejected=false;
    try { fn(); } catch (const GhostSaveError&) { rejected=true; }
    assert(rejected);
}
GhostRecord pose(double time, Vec3 position={}, Vec3 angles={}, int action=0, double frame=0, int mode=0) {
    return {v(time),c4::physics::source(position),c4::physics::source(angles),action,v(frame),mode};
}
GhostTrack track() {
    GhostTrack value;
    value.name="Driver";
    value.record_ticks=v(32);
    value.checkpoint_ticks={v(1),v(2),v(3),v(4),v(5),v(6),v(7),v(8),v(9)};
    value.records={pose(0,{1,2,3},{350,170,-170},1,90,0),
                   pose(16,{17,18,19},{10,-170,170},1,10,1),
                   pose(32,{33,34,35},{30,-150,150},2,75,1)};
    return value;
}
void codec() {
    // Fixed original-WDL fixture (key '$', name 'A', decimal tokens, full sentinel).
    constexpr std::string_view original =
        "ABCD~EFGH~IJKL~$~C~joz&'~kw~p~v~|~(~.~4~:~@~F~L~S~Z~a~gpr~j~p~w~+)~"
        "(~/;~5B~;I~AP~GM~L~R~Y~_e~e~gw$+~`m~ft~l{~rx~w~}~*~06~6~";
    const auto imported=parse_ghost(original);
    assert(imported.name=="A" && imported.record_ticks==v(16) && imported.records.size()==2);
    assert(imported.records[0].angles.x==v(350) && imported.records[0].frame==v(90));
    assert(imported.records[1].position.x==v(17) && imported.records[1].mode==1);
    assert(imported.records[1].action==1 && imported.records[1].frame==v(10));
    auto value=track();
    const auto encoded=serialize_ghost(value);
    assert(encoded.starts_with("C4RC~GHOST~LOCAL~G~"));
    const auto loaded=parse_ghost(encoded);
    assert(loaded.name==value.name && loaded.record_ticks==v(32));
    assert(loaded.checkpoint_ticks==value.checkpoint_ticks && loaded.records.size()==3);
    for (std::size_t i=0; i<3; ++i) {
        assert(loaded.records[i].time==value.records[i].time);
        assert(loaded.records[i].position.z==value.records[i].position.z);
        assert(loaded.records[i].angles.x==value.records[i].angles.x);
        assert(loaded.records[i].action==value.records[i].action);
        assert(loaded.records[i].frame==value.records[i].frame);
        assert(loaded.records[i].mode==value.records[i].mode);
    }
    auto fractional=track();
    fractional.records[0].position.x=v(1.001);
    assert(parse_ghost(serialize_ghost(fractional)).records[0].position.x==Var{1025});
    fractional.records[0].position.x=v(0.0005);
    assert(parse_ghost(serialize_ghost(fractional)).records[0].position.x==v(0));
    rejects([] { parse_ghost(""); });
    rejects([&] { parse_ghost(encoded.substr(0,encoded.size()-1)); });
    rejects([&] { parse_ghost(encoded+"JUNK~"); });
    auto corrupt=encoded;
    corrupt[18]='\0';
    rejects([&] { parse_ghost(corrupt); });
    value.records[1].time=v(0);
    rejects([&] { serialize_ghost(value); });
    value=track(); value.records[1].action=12;
    rejects([&] { serialize_ghost(value); });
    value=track(); value.records[1].mode=2;
    rejects([&] { serialize_ghost(value); });
    value=track(); value.records[1].frame=v(-1);
    rejects([&] { serialize_ghost(value); });
    value=track(); value.name="not representable";
    rejects([&] { serialize_ghost(value); });
    value=track(); value.records.clear(); value.record_ticks=v(ghost_capacity);
    for (std::size_t i=0; i<ghost_import_capacity; ++i)
        value.records.push_back(pose(static_cast<double>(i)));
    assert(parse_ghost(serialize_ghost(value)).records.size()==ghost_import_capacity);
    value.records.push_back(pose(static_cast<double>(ghost_import_capacity)));
    const auto native_full=serialize_ghost(value);
    rejects([&] { parse_ghost(native_full); });
    save_rejects([&] { save_local_ghost(value); });
    value.records.push_back(pose(static_cast<double>(ghost_capacity)));
    rejects([&] { serialize_ghost(value); });
}
void replay() {
    auto value=track();
    assert(!sample_ghost(value,v(-1)) && !sample_ghost(value,v(32.001)));
    const auto middle=sample_ghost(value,v(8));
    assert(middle && middle->record.position.x==v(9) && middle->record.angles.x==v(360));
    assert(middle->record.angles.y==v(180) && middle->record.angles.z==v(-180));
    assert(middle->record.mode==0 && middle->record.action==1 && middle->record.frame==v(100));
    assert(!middle->action_transition && middle->blend==v(0.5));
    const auto one_third=v(1)/v(3);
    assert(sample_ghost(value,one_third)->record.position.x==v(1)+one_third);
    const auto wrap=sample_ghost(value,v(12));
    assert(wrap && wrap->record.frame==v(5));
    const auto boundary=sample_ghost(value,v(16));
    assert(boundary && boundary->record.mode==1 && boundary->record.action==1);
    const auto transition=sample_ghost(value,v(24));
    assert(transition && transition->action_transition && transition->record.action==1);
    assert(transition->record.frame==v(10) && transition->next_action==2 && transition->next_frame==v(75));
    assert(transition->blend==v(0.5));
    const auto end=sample_ghost(value,v(32));
    assert(end && end->record.action==2 && end->record.frame==v(75) && !end->action_transition);
    assert(ghost_alpha(v(9),v(10))==v(0) && ghost_alpha(v(10),v(10))==v(10));
    assert(ghost_alpha(v(200),v(200))==v(0) && ghost_alpha(v(280),v(200))==v(10));
    assert(ghost_alpha(v(3000),v(10))==v(50) && ghost_alpha(v(3500),v(10))==v(100));
}
void persistence() {
    char temporary[]="/tmp/c4-ghost-check-XXXXXX";
    const auto* root=::mkdtemp(temporary);
    assert(root);
    const char* old=std::getenv("XDG_DATA_HOME");
    const bool had_old=old!=nullptr;
    const std::string previous=old ? old : "";
    assert(::setenv("XDG_DATA_HOME",root,1)==0);
    const auto path=local_ghost_path();
    assert(!load_ghost(path));
    const auto original=std::filesystem::path(root)/"original-bhlt.rc4";
    {
        std::ofstream file(original,std::ios::binary);
        file << serialize_ghost(track());
        assert(file.good());
    }
    save_local_ghost(track());
    assert(load_ghost(path)->name=="Driver");
    auto better=track(); better.name="Better";
    save_local_ghost(better);
    assert(load_ghost(path)->name=="Better");
    assert(load_ghost(original)->name=="Driver");
    assert(!load_ghost_archive());
    auto full=track();
    full.record_ticks=v(ghost_capacity);
    full.records.clear();
    for (std::size_t i=0; i<ghost_capacity; ++i)
        full.records.push_back(pose(static_cast<double>(i),{static_cast<double>(i),2,3},{4,5,6},
                                   static_cast<int>(i%12),static_cast<double>(i%100),static_cast<int>(i%2)));
    save_ghost_archive(full);
    const auto archive=load_ghost_archive();
    assert(archive && archive->records.size()==ghost_capacity && archive->record_ticks==full.record_ticks);
    assert(archive->records.back().position.x==v(ghost_capacity-1));
    assert(archive->records.back().action==full.records.back().action);
    assert(archive->records.back().frame==full.records.back().frame);
    assert(archive->records.back().mode==full.records.back().mode);
    assert(local_ghost_archive_path().filename()=="native-recording.rc4");
    rejects([&] { load_ghost(local_ghost_archive_path()); });
    assert(load_ghost(path)->name=="Better" && load_ghost(original)->name=="Driver");
    full.records.push_back(pose(static_cast<double>(ghost_capacity)));
    save_rejects([&] { save_ghost_archive(full); });
    assert(load_ghost_archive()->records.size()==ghost_capacity);
    std::filesystem::create_symlink(original,std::filesystem::path(root)/"linked.rc4");
    bool rejected=false;
    try { load_ghost(std::filesystem::path(root)/"linked.rc4"); }
    catch (const std::system_error&) { rejected=true; }
    assert(rejected);
    std::filesystem::remove_all(path.parent_path());
    std::filesystem::create_directory_symlink(root,path.parent_path());
    rejected=false;
    try { save_local_ghost(better); }
    catch (const GhostSaveError&) { rejected=true; }
    assert(rejected && load_ghost(original)->name=="Driver");
    rejected=false;
    try { save_ghost_archive(better); }
    catch (const GhostSaveError&) { rejected=true; }
    assert(rejected && load_ghost(original)->name=="Driver");
    if (had_old) assert(::setenv("XDG_DATA_HOME",previous.c_str(),1)==0);
    else assert(::unsetenv("XDG_DATA_HOME")==0);
    std::filesystem::remove_all(root);
}
} // namespace
int main() { codec(); replay(); persistence(); }
