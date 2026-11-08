#include "Assets.hpp"
#include "Resources.hpp"
#include "Animation.hpp"
#include "SkinImages.hpp"
#include "GpuImage.hpp"
#include "GpuImageBc.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <limits>
#include <utility>

namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void require_metadata(const c4::assets::Image& image, c4::assets::ImageSourceFormat format,
                      std::uint8_t bits_per_pixel) {
    require(image.face_count==1 && image.source_format==format &&
            image.source_bits_per_pixel==bits_per_pixel, "image source metadata mismatch");
}
void u16(Bytes& b, std::uint16_t n) { b.push_back(n & 255); b.push_back(n >> 8); }
void u32(Bytes& b, std::uint32_t n) { for (int i = 0; i < 4; ++i) b.push_back((n >> (8*i)) & 255); }
void be32(Bytes& b, std::uint32_t n) { for (int i = 3; i >= 0; --i) b.push_back((n >> (8*i)) & 255); }
void put32(Bytes& b, std::size_t at, std::uint32_t n) { for (int i = 0; i < 4; ++i) b.at(at+i) = (n >> (8*i)) & 255; }
void f32(Bytes& b, float n) { u32(b, std::bit_cast<std::uint32_t>(n)); }
void save(const std::filesystem::path& p, const Bytes& b) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!f) throw std::runtime_error("cannot write test input");
}
template<class Fn> void rejects(Fn fn) {
    bool rejected = false;
    try { fn(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "malformed asset accepted");
}
struct Temp {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("c4-assets-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { require(std::filesystem::create_directory(dir), "test temp directory unavailable"); }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};
Bytes wrs(std::string name, const Bytes& packed, std::uint32_t raw) {
    Bytes b{'W','R','S','3'};
    for (char c : name) b.push_back(static_cast<std::uint8_t>(c)^0xb5);
    b.resize(36); be32(b, static_cast<std::uint32_t>(packed.size())); be32(b, raw);
    b.insert(b.end(), packed.begin(), packed.end()); return b;
}
Bytes mdl() {
    Bytes b{'M','D','L','7'};
    u32(b,0); u32(b,0); u32(b,1); u32(b,0); u32(b,0); u32(b,0);
    for (auto n : {36,28,16,68,8,16,26,26,68,24}) u16(b,static_cast<std::uint16_t>(n));
    u32(b,1); u32(b,0); b.resize(72);
    for (auto n : {1,3,1,3,1}) u32(b,static_cast<std::uint32_t>(n));
    u32(b,7); u32(b,6); u32(b,1); b.resize(120);
    for (char c : std::string("a.dds")) b.push_back(static_cast<std::uint8_t>(c)); b.push_back(0);
    for (auto n : {0.f,0.f,1.f,0.f,0.f,1.f}) f32(b,n);
    for (auto n : {0,1,2,0,1,2}) u16(b,static_cast<std::uint16_t>(n)); u32(b,0);
    for (auto xyz : {std::array<float,3>{0,0,0}, {1,0,0}, {0,1,0}}) {
        for (auto n : xyz) f32(b,n); u16(b,0xffff);
        for (auto n : {0.f,0.f,1.f}) f32(b,n);
    }
    b.resize(b.size()+24); // One frame with no edits or bone transforms.
    put32(b,16,static_cast<std::uint32_t>(b.size()));
    put32(b,52,static_cast<std::uint32_t>(b.size()-48));
    return b;
}
void source_tangent_behavior() {
    using c4::assets::Vertex;
    using c4::assets::generate_source_tangents;
    const auto near = [](float a, float b) { return std::abs(a-b)<2e-6f; };
    const std::array<std::uint32_t,3> triangle{0,1,2};
    std::vector<Vertex> vertices{{0,0,0,0,0,0,0,1},
                                 {1,0,0,1,0,0,0,1},
                                 {0,1,0,0,1,0,0,1},
                                 {0,0,0,0,0,0,0,1}};
    std::vector<std::array<float,4>> channel(4,{7,8,9,.25f});
    channel.reserve(16);
    const auto* storage=channel.data();
    const auto capacity=channel.capacity();
    generate_source_tangents(vertices,triangle,channel);
    for (std::size_t i=0;i<3;++i)
        require(near(channel[i][0],1) && channel[i][1]==0 && channel[i][2]==0 &&
                channel[i][3]==1,"corner permutations or D3D tangent basis mismatch");
    require(channel[3]==std::array<float,4>{0,0,0,.25f},
            "unreferenced tangent xyz/w mismatch");
    require(channel.data()==storage && channel.capacity()==capacity,
            "tangent generation replaced reusable storage");

    // A duplicated position with a distinct UV is a distinct source index.
    vertices[3]=vertices[0];
    vertices.push_back({1,0,0,-1,0,0,0,1});
    vertices.push_back(vertices[2]);
    const std::array<std::uint32_t,6> seams{0,1,2,3,4,5};
    generate_source_tangents(vertices,seams,channel);
    require(near(channel[0][0],1) && near(channel[3][0],-1) &&
            channel[0][3]==1 && channel[3][3]==-1,
            "UV seam indices were welded");

    // Opposite V orientation retains xyz but overwrites w in face order.
    vertices[3]=vertices[1]; vertices[4]=vertices[2]; vertices[4].v=-1;
    const std::array<std::uint32_t,6> faces{0,1,2,0,3,4};
    const std::array<std::uint32_t,6> reversed_faces{0,3,4,0,1,2};
    generate_source_tangents(vertices,faces,channel);
    require(near(channel[0][0],1) && channel[0][3]==-1,"last incident w mismatch");
    generate_source_tangents(vertices,reversed_faces,channel);
    require(near(channel[0][0],1) && channel[0][3]==1,"face-order w was averaged");

    // Each numerator is normalized before summation, regardless of UV scale.
    vertices[3]={0,1,0,8,0,0,0,1};
    vertices[4]={1,0,0,0,8,0,0,1};
    generate_source_tangents(vertices,faces,channel);
    require(near(channel[0][0],std::sqrt(.5f)) && channel[0][1]==0 &&
            near(channel[0][2],std::sqrt(.5f)),
            "corner sum was weighted by numerator length or determinant");

    vertices.resize(3);
    vertices[0]={0,0,0,0,0,0,0,1};
    vertices[1]={1,0,0,1,1,0,0,1};
    vertices[2]={0,1,0,2,2,0,0,1};
    generate_source_tangents(vertices,triangle,channel);
    require(near(channel[0][0],2/std::sqrt(5.f)) && channel[0][1]==0 &&
            near(channel[0][2],-1/std::sqrt(5.f)),
            "rank-one UV numerator was discarded");
    for (auto& vertex:vertices) vertex.u=vertex.v=0;
    generate_source_tangents(vertices,triangle,channel);
    for (const auto& tangent:channel)
        require(tangent==std::array<float,4>{0,0,0,1},
                "identical UVs synthesized a tangent axis");
    vertices[1].u=1; vertices[2].v=1;
    vertices[1].x=0; vertices[2].y=0;
    generate_source_tangents(vertices,triangle,channel);
    require(channel[0]==std::array<float,4>{0,0,0,1},"zero geometry fallback mismatch");

    vertices[1].x=1; vertices[2].y=1;
    vertices.push_back({1,0,0,-1,0,0,0,1});
    vertices.push_back(vertices[2]);
    generate_source_tangents(vertices,faces,channel);
    require(channel[0]==std::array<float,4>{0,0,0,-1},
            "opposing corner cancellation synthesized an axis or changed w");
    vertices.resize(3);
    vertices[0].nx=1; // Nonunit n=(1,0,1) projects +X to D3D -Y.
    generate_source_tangents(vertices,triangle,channel);
    require(channel[0][0]==0 && near(channel[0][1],-1) && channel[0][2]==0,
            "generator normalized its input normal");
    vertices[0].nx=0;
    vertices[1].x=0x1p-23f;
    generate_source_tangents(vertices,triangle,channel);
    require(near(channel[0][0],1),"inclusive SSE2 normalization threshold mismatch");
    vertices[1].x=0x1p-24f;
    generate_source_tangents(vertices,triangle,channel);
    require(channel[0]==std::array<float,4>{0,0,0,1},
            "below-threshold tangent was normalized");

    rejects([&]{generate_source_tangents(vertices,std::span(triangle).first(2),channel);});
    const std::array<std::uint32_t,3> invalid{0,1,3};
    rejects([&]{generate_source_tangents(vertices,invalid,channel);});
    for (float* field : {&vertices[0].x,&vertices[0].y,&vertices[0].z,
                         &vertices[0].u,&vertices[0].v,&vertices[0].nx,
                         &vertices[0].ny,&vertices[0].nz}) {
        const float saved=*field;
        for (float invalid_value : {std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::infinity()}) {
            *field=invalid_value;
            rejects([&]{generate_source_tangents(vertices,triangle,channel);});
        }
        *field=saved;
    }
    vertices[0].x=-std::numeric_limits<float>::max();
    vertices[1].x=std::numeric_limits<float>::max();
    rejects([&]{generate_source_tangents(vertices,triangle,channel);});
    vertices[0].x=0; vertices[1].x=1;
    vertices.push_back({0,0,0,0,0,0,0,1});
    channel.resize(4); channel[3][3]=std::numeric_limits<float>::infinity();
    rejects([&]{generate_source_tangents(vertices,triangle,channel);});
    channel.clear();
    generate_source_tangents(vertices,triangle,channel);
    require(channel[3]==std::array<float,4>{0,0,0,0},
            "native new orphan storage initialization mismatch");
}
void skin_behavior() {
    using c4::assets::Skin;
    using c4::assets::decode_skin_image;
    Skin skin;
    skin.format=2; skin.flags=2; skin.width=2; skin.height=1;
    skin.payload={0,0xf8,0xe0,7};
    const auto rgb565 = decode_skin_image(skin);
    require(rgb565.rgba==Bytes({255,0,0,255,0,255,0,255}), "skin RGB565 mismatch");
    require_metadata(rgb565,c4::assets::ImageSourceFormat::ModelSkin,16);
    skin.flags=0x32; skin.material={0.25f}; skin.effect="original effect";
    require(decode_skin_image(skin).rgba==Bytes({255,0,0,255,0,255,0,255}), "skin metadata changed pixels");
    skin.width=1; skin.payload={0x82,0x10}; // Original c4_arac_box.MDL first RGB565 pixel.
    require(decode_skin_image(skin).rgba==Bytes({16,16,16,255}), "skin RGB565 bit replication mismatch");
    skin.format=3; skin.flags=3; skin.width=1; skin.payload={0x23,0x81};
    const auto argb4444 = decode_skin_image(skin);
    require(argb4444.rgba==Bytes({17,34,51,136}), "skin ARGB4444 alpha mismatch");
    require_metadata(argb4444,c4::assets::ImageSourceFormat::ModelSkin,16);
    skin.format=4; skin.flags=4; skin.payload={3,2,1};
    const auto bgr = decode_skin_image(skin);
    require(bgr.rgba==Bytes({1,2,3,255}), "skin BGR mismatch");
    require_metadata(bgr,c4::assets::ImageSourceFormat::ModelSkin,24);
    skin.format=5; skin.flags=5; skin.payload={3,2,1,4};
    const auto bgra = decode_skin_image(skin);
    require(bgra.rgba==Bytes({1,2,3,4}), "skin BGRA alpha mismatch");
    require_metadata(bgra,c4::assets::ImageSourceFormat::ModelSkin,32);
    skin.payload.pop_back(); rejects([&]{ decode_skin_image(skin); });
    skin.payload={3,2,1,4,5}; rejects([&]{ decode_skin_image(skin); });
    skin.width=8; skin.height=8; skin.flags=13;
    skin.payload=Bytes((64+16+4+1)*4,0);
    skin.payload[0]=3; skin.payload[1]=2; skin.payload[2]=1; skin.payload[3]=4;
    Bytes expected_lower;
    for (std::uint8_t pixel=0; pixel<21; ++pixel) {
        const auto at=64*4+std::size_t(pixel)*4;
        skin.payload[at]=10+pixel; skin.payload[at+1]=40+pixel;
        skin.payload[at+2]=80+pixel; skin.payload[at+3]=120+pixel;
        expected_lower.insert(expected_lower.end(),{
            static_cast<std::uint8_t>(80+pixel),static_cast<std::uint8_t>(40+pixel),
            static_cast<std::uint8_t>(10+pixel),static_cast<std::uint8_t>(120+pixel)});
    }
    const auto mip = decode_skin_image(skin);
    Bytes expected_base(64*4);
    expected_base[0]=1; expected_base[1]=2; expected_base[2]=3; expected_base[3]=4;
    require(mip.width==8 && mip.height==8 && mip.rgba==expected_base, "skin base mip mismatch");
    require(mip.mip_rgba.size()==84 && mip.mip_rgba==expected_lower,
            "skin stored lower mip color/alpha mismatch");
    require(mip.mip_levels.size()==3 &&
            mip.mip_levels[0].width==4 && mip.mip_levels[0].height==4 && mip.mip_levels[0].offset==0 &&
            mip.mip_levels[1].width==2 && mip.mip_levels[1].height==2 && mip.mip_levels[1].offset==64 &&
            mip.mip_levels[2].width==1 && mip.mip_levels[2].height==1 && mip.mip_levels[2].offset==80,
            "skin stored lower mip records mismatch");
    require_metadata(mip,c4::assets::ImageSourceFormat::ModelSkin,32);
    skin.payload.pop_back(); rejects([&]{ decode_skin_image(skin); });
    skin.payload.resize((64+16+4+1)*4+1); rejects([&]{ decode_skin_image(skin); });
    skin.width=7; rejects([&]{ decode_skin_image(skin); });
    skin.flags=5; skin.width=0; rejects([&]{ decode_skin_image(skin); });
    skin.width=16385; rejects([&]{ decode_skin_image(skin); });
    skin.width=8193; skin.height=8193; rejects([&]{ decode_skin_image(skin); });
    skin.width=0xffffffffu; skin.height=0xffffffffu; rejects([&]{ decode_skin_image(skin); });
    skin.width=1; skin.height=1; skin.flags=4; rejects([&]{ decode_skin_image(skin); });
    skin.flags=0x85; rejects([&]{ decode_skin_image(skin); });
    skin.format=1; skin.flags=1; rejects([&]{ decode_skin_image(skin); });
    const auto rejects_message = [&](const char* message, bool exact = false) {
        try { decode_skin_image(skin); }
        catch (const std::runtime_error& error) {
            const std::string actual = error.what();
            require(exact ? actual==message : actual.find(message)!=std::string::npos,
                    "skin error lacks explanation");
            return;
        }
        require(false,"invalid skin accepted");
    };
    skin.width=8192; skin.height=8192; skin.format=5; skin.flags=13; skin.payload.clear();
    rejects_message("image: aggregate decoded image exceeds 256 MiB",true);
    skin.width=1; skin.height=1;
    skin.format=0; skin.flags=0; skin.payload={1};
    rejects_message("palette");
    std::array<std::uint8_t,768> palette{};
    palette[3]=1; palette[4]=2; palette[5]=3;
    const auto indexed = decode_skin_image(skin,palette);
    require(indexed.rgba==Bytes({1,2,3,255}), "skin original RGB palette mismatch");
    require_metadata(indexed,c4::assets::ImageSourceFormat::ModelSkin,8);
    rejects([&]{ decode_skin_image(skin,std::span<const std::uint8_t>(palette).first(767)); });
    skin.format=6; skin.flags=6; skin.height=1;
    skin.payload=Bytes(128);
    put32(skin.payload,0,0x20534444); put32(skin.payload,4,124); put32(skin.payload,8,0x81007);
    put32(skin.payload,12,4); put32(skin.payload,16,4); put32(skin.payload,20,8);
    put32(skin.payload,76,32); put32(skin.payload,80,4); put32(skin.payload,84,0x31545844);
    put32(skin.payload,108,0x1000);
    skin.payload.insert(skin.payload.end(),{0,0xf8,0xe0,7,0,0,0,0});
    skin.width=static_cast<std::uint32_t>(skin.payload.size());
    Bytes red(64); for (std::size_t n=0;n<16;++n) { red[n*4]=255; red[n*4+3]=255; }
    const auto embedded = decode_skin_image(skin); // No file/resource/temp path involved.
    require(embedded.width==4 && embedded.height==4 && embedded.rgba==red, "skin embedded DDS mismatch");
    require_metadata(embedded,c4::assets::ImageSourceFormat::Dds,0);
    --skin.width; rejects([&]{ decode_skin_image(skin); }); ++skin.width;
    skin.height=0; rejects([&]{ decode_skin_image(skin); }); skin.height=1;
    skin.payload.pop_back(); --skin.width; rejects([&]{ decode_skin_image(skin); });
    skin.payload.push_back(0); ++skin.width;
    skin.payload.push_back(0); ++skin.width; rejects([&]{ decode_skin_image(skin); });
    skin.payload.pop_back(); --skin.width;
    put32(skin.payload,8,0xa1007); put32(skin.payload,28,2); put32(skin.payload,108,0x401008);
    skin.payload.insert(skin.payload.end(),{0x1f,0,0xff,0xff,0xe4,0x1b,0,0}); skin.width+=8;
    const auto embedded_mip = decode_skin_image(skin);
    require(embedded_mip.rgba==red, "skin DDS emitted lower mip pixels");
    require(embedded_mip.mip_levels.size()==1 && embedded_mip.mip_levels[0].width==2 &&
            embedded_mip.mip_levels[0].height==2 && embedded_mip.mip_levels[0].offset==0,
            "skin DDS stored lower mip record mismatch");
    require(embedded_mip.mip_rgba.size()==16 &&
            embedded_mip.mip_rgba==Bytes({0,0,255,255,255,255,255,255,0,0,0,0,127,127,255,255}),
            "skin DDS stored lower mip block color/alpha mismatch");
    require_metadata(embedded_mip,c4::assets::ImageSourceFormat::Dds,0);
    skin.flags=14; rejects([&]{ decode_skin_image(skin); });
    skin.format=7; skin.flags=7; skin.external_texture="original.dds"; skin.payload={};
    rejects_message("external_reference");
}
void upper_left_color_key() {
    using c4::assets::Image;
    using c4::assets::apply_upper_left_color_key;
    const auto require_layout = [](const Image& image, const Image& before) {
        require(image.width==before.width && image.height==before.height &&
                image.face_count==before.face_count && image.source_format==before.source_format &&
                image.source_bits_per_pixel==before.source_bits_per_pixel &&
                image.source_alpha_known==before.source_alpha_known &&
                image.source_alpha_flag==before.source_alpha_flag &&
                image.mip_levels.size()==before.mip_levels.size(),
                "color key changed image metadata or generated mip levels");
        for (std::size_t n=0;n<before.mip_levels.size();++n)
            require(image.mip_levels[n].width==before.mip_levels[n].width &&
                    image.mip_levels[n].height==before.mip_levels[n].height &&
                    image.mip_levels[n].offset==before.mip_levels[n].offset,
                    "color key changed stored mip layout");
    };
    const auto apply_in_place = [&](Image& image) {
        const auto before=image;
        const auto* base=image.rgba.data();
        const auto* lower=image.mip_rgba.data();
        const auto* levels=image.mip_levels.data();
        const auto base_capacity=image.rgba.capacity();
        const auto lower_capacity=image.mip_rgba.capacity();
        const auto levels_capacity=image.mip_levels.capacity();
        apply_upper_left_color_key(image);
        require_layout(image,before);
        require(image.rgba.data()==base && image.mip_rgba.data()==lower &&
                image.mip_levels.data()==levels && image.rgba.capacity()==base_capacity &&
                image.mip_rgba.capacity()==lower_capacity &&
                image.mip_levels.capacity()==levels_capacity,
                "color key replaced image storage");
        const auto require_pixels = [&](const Bytes& pixels, const Bytes& original) {
            require(pixels.size()==original.size(),"color key changed pixel payload size");
            for (std::size_t n=0;n<original.size();n+=4) {
                require(pixels[n]==original[n] && pixels[n+1]==original[n+1] &&
                        pixels[n+2]==original[n+2],"color key changed RGB");
                const bool matches=original[n]==before.rgba[0] &&
                    original[n+1]==before.rgba[1] && original[n+2]==before.rgba[2];
                require(pixels[n+3]==(matches ? 0 : original[n+3]),
                        "color key did not preserve authored alpha or use base RGB exactly");
            }
        };
        require_pixels(image.rgba,before.rgba);
        require_pixels(image.mip_rgba,before.mip_rgba);
    };
    Image base{2,2,{17,34,51,201,17,34,51,77,17,34,52,41,17,35,51,93}};
    base.source_format=c4::assets::ImageSourceFormat::Tga;
    base.source_bits_per_pixel=24;
    apply_in_place(base);
    require(base.rgba==Bytes({17,34,51,0,17,34,51,0,17,34,52,41,17,35,51,93}) &&
            base.mip_rgba.empty() && base.mip_levels.empty(),"base color key mismatch");

    Image chain{4,4,Bytes(4*4*4)};
    chain.source_format=c4::assets::ImageSourceFormat::Bmp;
    chain.source_bits_per_pixel=24;
    for (std::size_t n=0;n<chain.rgba.size();n+=4) {
        chain.rgba[n]=17; chain.rgba[n+1]=34; chain.rgba[n+2]=52;
        chain.rgba[n+3]=static_cast<std::uint8_t>(100+n);
    }
    chain.rgba[2]=51; chain.rgba[3]=201;
    chain.rgba[10]=51; chain.rgba[11]=67;
    chain.mip_rgba={90,80,70,151,17,34,51,61,17,34,52,91,0,0,0,0,17,34,51,181};
    chain.mip_levels={{2,2,0},{1,1,16}};
    const auto raw=chain;
    auto overlay=raw; // The caller, not the primitive, owns the raw/overlay split.
    apply_in_place(overlay);
    require(overlay.mip_rgba==Bytes({90,80,70,151,17,34,51,0,17,34,52,91,0,0,0,0,
                                   17,34,51,0}),
            "lower mip color key used a per-level key instead of the base key");
    require_layout(raw,chain);
    require(raw.rgba==chain.rgba && raw.mip_rgba==chain.mip_rgba &&
            raw.rgba[3]==201 && raw.mip_rgba[7]==61 && raw.mip_rgba[19]==181,
            "overlay color key mutated the raw caller source");

    const auto reject_unchanged = [&](Image image) {
        const auto before=image;
        rejects([&]{ apply_upper_left_color_key(image); });
        require_layout(image,before);
        require(image.rgba==before.rgba && image.mip_rgba==before.mip_rgba,
                "invalid color key image was partially mutated");
    };
    for (const std::uint32_t faces : {0u,2u,6u}) {
        auto invalid=chain; invalid.face_count=faces; reject_unchanged(invalid);
    }
    for (const auto dimensions : {std::array<std::uint32_t,2>{0,4},{4,0},{16385,4},
                                  {4,16385},{16384,8192},{0xffffffffu,0xffffffffu}}) {
        auto invalid=chain; invalid.width=dimensions[0]; invalid.height=dimensions[1];
        reject_unchanged(invalid);
    }
    auto invalid=chain; invalid.rgba.pop_back(); reject_unchanged(invalid);
    invalid=chain; invalid.rgba.push_back(0); reject_unchanged(invalid);
    invalid=chain; invalid.rgba.clear(); reject_unchanged(invalid);
    invalid=chain; invalid.mip_levels[0].width=1; reject_unchanged(invalid);
    invalid=chain; invalid.mip_levels[0].height=0; reject_unchanged(invalid);
    invalid=chain; invalid.mip_levels[1].width=2; reject_unchanged(invalid);
    invalid=chain; invalid.mip_levels[1].height=2; reject_unchanged(invalid);
    invalid=chain; invalid.mip_levels[0].offset=1; reject_unchanged(invalid);
    for (const std::size_t offset : {std::size_t{0},std::size_t{15},std::size_t{17}}) {
        invalid=chain; invalid.mip_levels[1].offset=offset; reject_unchanged(invalid);
    }
    invalid=chain; invalid.mip_rgba.pop_back(); reject_unchanged(invalid);
    invalid=chain; invalid.mip_rgba.push_back(0); reject_unchanged(invalid);
    invalid=chain; invalid.mip_rgba.clear(); reject_unchanged(invalid);
    invalid=chain; invalid.mip_levels.clear(); reject_unchanged(invalid);
    invalid=chain; invalid.mip_levels.push_back({1,1,20});
    invalid.mip_rgba.insert(invalid.mip_rgba.end(),{17,34,51,211}); reject_unchanged(invalid);
}
void behavior() {
    const c4::assets::Image aggregate{1,1,{1,2,3,4}};
    const c4::assets::Image empty{};
    require_metadata(aggregate,c4::assets::ImageSourceFormat::Unknown,0);
    require(empty.mip_rgba.empty() && empty.mip_levels.empty() &&
            aggregate.mip_rgba.empty() && aggregate.mip_levels.empty(),
            "default image fabricated stored lower mip levels");
    Temp temp; auto p = temp.dir/"input";
    // LZSS literal then overlapping reference into its own output.
    auto archive = wrs("a.bin", {1,'A',0xee,0xf2}, 6);
    save(p,archive);
    require(c4::assets::list_resources(p).size()==1, "WRS directory mismatch");
    require(c4::assets::read_resource(p.string()+"::a.bin")==Bytes(6,'A'), "LZSS overlap mismatch");
    rejects([&]{ c4::assets::read_resource(p.string()+"::../a.bin"); });
    save(p,wrs("../a.bin",{1,'A'},1)); rejects([&]{ c4::assets::list_resources(p); });
    for (std::size_t n=0;n<archive.size();++n) {
        if (n==4) continue; // Empty WRS3 archive is valid.
        save(p,Bytes(archive.begin(),archive.begin()+n)); rejects([&]{ c4::assets::read_resource(p.string()+"::a.bin"); });
    }
    p = temp.dir/"input.tga"; // Exercise extension-dispatched consumers with actual format names.
    // Bottom-left BGR TGA is normalized to top-left RGBA.
    Bytes tga(18); tga[2]=2; tga[12]=2; tga[14]=2; tga[16]=24;
    tga.insert(tga.end(),{255,0,0,255,255,255,0,0,255,0,255,0}); save(p,tga);
    const auto image = c4::assets::load_image(p);
    require(image.width==2 && image.height==2, "TGA dimensions mismatch");
    require(image.rgba==Bytes({255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255}), "TGA origin/BGR mismatch");
    require_metadata(image,c4::assets::ImageSourceFormat::Tga,24);
    require(image.mip_rgba.empty() && image.mip_levels.empty(), "TGA fabricated stored lower mip levels");
    for (std::size_t n=0;n<tga.size();++n) {
        save(p,Bytes(tga.begin(),tga.begin()+n)); rejects([&]{ c4::assets::load_image(p); });
    }
    tga[2]=10; tga.resize(18); tga.insert(tga.end(),{0x84,0,0,0}); save(p,tga);
    rejects([&]{ c4::assets::load_image(p); }); // Five-pixel run in a four-pixel image.
    // A true 32-bit header retains alpha, rather than inferring depth from RGBA output.
    Bytes tga32(18); tga32[2]=2; tga32[12]=2; tga32[14]=1; tga32[16]=32; tga32[17]=0x28;
    tga32.insert(tga32.end(),{3,2,1,4,6,5,4,7}); save(p,tga32);
    const auto image32 = c4::assets::load_image(p);
    require(image32.width==2 && image32.height==1 &&
            image32.rgba==Bytes({1,2,3,4,4,5,6,7}), "TGA32 BGRA mismatch");
    require_metadata(image32,c4::assets::ImageSourceFormat::Tga,32);
    p = temp.dir/"input.dds";
    Bytes dds(128); dds[0]='D';dds[1]='D';dds[2]='S';dds[3]=' ';
    put32(dds,4,124);put32(dds,8,0x81007);put32(dds,12,4);put32(dds,16,4);
    put32(dds,20,8);put32(dds,76,32);put32(dds,80,4);put32(dds,84,0x31545844);put32(dds,108,0x1000);
    dds.insert(dds.end(),{0,0xf8,0xe0,7,0xe4,0xe4,0xe4,0xe4}); save(p,dds);
    const auto block = c4::assets::load_image(p);
    require(block.width==4 && block.height==4, "DDS dimensions mismatch");
    require(block.rgba==Bytes({255,0,0,255,0,255,0,255,170,85,0,255,85,170,0,255,
        255,0,0,255,0,255,0,255,170,85,0,255,85,170,0,255,
        255,0,0,255,0,255,0,255,170,85,0,255,85,170,0,255,
        255,0,0,255,0,255,0,255,170,85,0,255,85,170,0,255}), "DXT1 color mismatch");
    require_metadata(block,c4::assets::ImageSourceFormat::Dds,0);
    dds.pop_back(); save(p,dds); rejects([&]{ c4::assets::load_image(p); });
    Bytes dds32(128); put32(dds32,0,0x20534444); put32(dds32,4,124);
    put32(dds32,8,0x100f); put32(dds32,12,1); put32(dds32,16,2); put32(dds32,20,8);
    put32(dds32,76,32); put32(dds32,80,0x41); put32(dds32,88,32);
    put32(dds32,92,0xff0000); put32(dds32,96,0xff00); put32(dds32,100,0xff);
    put32(dds32,104,0xff000000); put32(dds32,108,0x1000);
    dds32.insert(dds32.end(),{3,2,1,4,6,5,4,7}); save(p,dds32);
    const auto raw_dds = c4::assets::load_image(p);
    require(raw_dds.width==2 && raw_dds.height==1 &&
            raw_dds.rgba==Bytes({1,2,3,4,4,5,6,7}), "uncompressed DDS32 BGRA mismatch");
    require_metadata(raw_dds,c4::assets::ImageSourceFormat::Dds,32);
    auto pitched_mips=dds32;
    put32(pitched_mips,8,0x2100f); put32(pitched_mips,20,12);
    put32(pitched_mips,28,2); put32(pitched_mips,108,0x401008);
    pitched_mips.insert(pitched_mips.end(),{201,202,203,204,9,8,7,6}); save(p,pitched_mips);
    const auto raw_mips = c4::assets::load_image(p);
    require(raw_mips.width==2 && raw_mips.height==1 && raw_mips.rgba==raw_dds.rgba,
            "pitched DDS lower mip or padding leaked into base pixels");
    require(raw_mips.mip_levels.size()==1 && raw_mips.mip_levels[0].width==1 &&
            raw_mips.mip_levels[0].height==1 && raw_mips.mip_levels[0].offset==0 &&
            raw_mips.mip_rgba==Bytes({7,8,9,6}), "pitched DDS stored lower mip mismatch");
    require_metadata(raw_mips,c4::assets::ImageSourceFormat::Dds,32);
    p = temp.dir/"input.bmp";
    Bytes bmp(54); bmp[0]='B'; bmp[1]='M';
    put32(bmp,2,70); put32(bmp,10,54); put32(bmp,14,40);
    put32(bmp,18,2); put32(bmp,22,2); bmp[26]=1; bmp[28]=24; put32(bmp,34,16);
    bmp.insert(bmp.end(),{255,0,0,255,255,255,0,0,0,0,255,0,255,0,0,0}); save(p,bmp);
    const auto bitmap = c4::assets::load_image(p);
    require(bitmap.width==2 && bitmap.height==2, "BMP dimensions mismatch");
    require(bitmap.rgba==image.rgba, "BMP origin/BGR/row padding mismatch");
    require_metadata(bitmap,c4::assets::ImageSourceFormat::Bmp,24);
    for (std::size_t n=0;n<bmp.size();++n) {
        save(p,Bytes(bmp.begin(),bmp.begin()+n)); rejects([&]{ c4::assets::load_image(p); });
    }
    bmp[0]=0; save(p,bmp); rejects([&]{ c4::assets::load_image(p); });
    Bytes bmp32(54); bmp32[0]='B'; bmp32[1]='M';
    put32(bmp32,2,62); put32(bmp32,10,54); put32(bmp32,14,40);
    put32(bmp32,18,2); put32(bmp32,22,1); bmp32[26]=1; bmp32[28]=32; put32(bmp32,34,8);
    bmp32.insert(bmp32.end(),{3,2,1,4,6,5,4,7}); save(p,bmp32);
    const auto bitmap32 = c4::assets::load_image(p);
    // BI_RGB has no declared alpha mask: its fourth byte does not become alpha.
    require(bitmap32.width==2 && bitmap32.height==1 &&
            bitmap32.rgba==Bytes({1,2,3,255,4,5,6,255}), "BMP32 BGR mismatch");
    require_metadata(bitmap32,c4::assets::ImageSourceFormat::Bmp,32);
    p = temp.dir/"input.png";
    save(p,{0x89,'P','N','G',13,10,26,10}); rejects([&]{ c4::assets::load_image(p); });
    p = temp.dir/"input";
    const auto model = mdl(); save(p,model); const auto mesh=c4::assets::load_mesh(p);
    require(mesh.vertices.size()==3 && mesh.indices.size()==3, "MDL7 geometry count mismatch");
    require(mesh.vertices[1].x==1 && mesh.vertices[1].u==1, "MDL7 position/UV mismatch");
    require(mesh.secondary_uv.size()==mesh.vertices.size(), "MDL7 source UV1 missing");
    for (std::size_t i=0;i<mesh.vertices.size();++i)
        require(mesh.secondary_uv[i]==std::array<float,2>{mesh.vertices[i].u,mesh.vertices[i].v},
                "MDL7 source UV1 differs from split vertex UV0");
    require(mesh.tertiary_uv.empty(), "MDL7 fabricated tertiary UV set");
    require(mesh.tangent_provenance==c4::assets::TangentProvenance::Unknown,
            "stock MDL tangent provenance was fabricated");
    for (std::size_t n=0;n<model.size();++n) {
        save(p,Bytes(model.begin(),model.begin()+n)); rejects([&]{ c4::assets::load_mesh(p); });
    }
    Bytes loose{'b','m','e','s','h'}; u32(loose,3);
    for (auto xyz : {std::array<float,3>{0,0,0}, {1,0,0}, {0,1,0}}) {
        for (auto n : xyz) f32(loose,n);
        for (auto n : {0.f,0.f,1.f,0.25f,0.75f,0.125f,0.375f,0.5f,0.625f,0.875f,1.25f}) f32(loose,n);
    }
    u32(loose,1); u16(loose,0); u16(loose,1); u16(loose,2); u32(loose,1); u32(loose,0);
    save(p,loose); const auto loose_mesh=c4::assets::load_mesh(p);
    require(loose_mesh.vertices.size()==3 && loose_mesh.indices.size()==3, "bmesh count mismatch");
    require(loose_mesh.vertices[1].x==1 && loose_mesh.vertices[1].u==0.25f &&
            loose_mesh.vertices[1].v==0.75f, "bmesh position/UV mismatch");
    require(loose_mesh.vertices[1].nz==1.f, "bmesh normal mismatch");
    require(loose_mesh.secondary_uv==std::vector<std::array<float,2>>(3, {0.125f,0.375f}),
            "bmesh secondary UV mismatch");
    require(loose_mesh.secondary_uv[1]!=std::array<float,2>{loose_mesh.vertices[1].u,loose_mesh.vertices[1].v},
            "bmesh authored UV1 overwritten by UV0");
    require(loose_mesh.tertiary_uv==std::vector<std::array<float,4>>(3, {0.5f,0.625f,0.875f,1.25f}),
            "bmesh tertiary UV mismatch");
    require(loose_mesh.tangent_provenance==c4::assets::TangentProvenance::ImportedSource &&
            c4::assets::read_resource(p)==loose,"bmesh tangent source/provenance was changed");
    loose.pop_back(); save(p,loose); rejects([&]{ c4::assets::load_mesh(p); });
}
Bytes cube_tga(std::uint16_t width, std::uint16_t height, bool top_left = true) {
    Bytes data(18);
    data[2]=2; data[12]=width&255; data[13]=width>>8;
    data[14]=height&255; data[15]=height>>8; data[16]=32;
    data[17]=top_left ? 0x28 : 0x08;
    for (unsigned row=0; row<height; ++row) {
        const auto y=top_left ? row : height-1-row;
        for (unsigned x=0; x<width; ++x)
            data.insert(data.end(),{static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(x%height),
                                   static_cast<std::uint8_t>(x/height), static_cast<std::uint8_t>(31+x+y)});
    }
    return data;
}
void cubemap_payload() {
    Temp temp; const auto path=temp.dir/"strip.tga";
    auto strip=cube_tga(12,2);
    Bytes expected;
    for (std::size_t pixel=0; pixel<24; ++pixel) {
        strip[18+pixel*4]=3; strip[19+pixel*4]=2;
        strip[20+pixel*4]=1; strip[21+pixel*4]=4;
        expected.insert(expected.end(),{1,2,3,4});
    }
    save(path,strip);
    const auto plain=c4::assets::load_image(path);
    require(plain.width==12 && plain.height==2 && plain.face_count==1 && plain.rgba==expected,
            "six-wide input was automatically converted by load_image");
    const auto cube=c4::assets::load_cubemap(path);
    require(cube.width==2 && cube.height==2 && cube.face_count==6 && cube.rgba==expected,
            "cubemap six-plane payload or exact alpha mismatch");
    const auto direct=c4::assets::cubemap_from_image(plain);
    require(direct.width==cube.width && direct.height==cube.height && direct.face_count==cube.face_count &&
            direct.rgba==cube.rgba && direct.source_format==cube.source_format &&
            direct.source_bits_per_pixel==cube.source_bits_per_pixel,
            "direct cubemap conversion differs from shared path conversion");
    require(cube.source_format==c4::assets::ImageSourceFormat::Tga && cube.source_bits_per_pixel==32,
            "cubemap lost original format/depth");
    Bytes packed;
    for (std::size_t offset=0; offset<strip.size(); offset+=8) {
        packed.push_back(0xff);
        const auto count=strip.size()-offset<8 ? strip.size()-offset : 8;
        packed.insert(packed.end(),strip.begin()+offset,strip.begin()+offset+count);
    }
    const auto archive=temp.dir/"source.rc4";
    save(archive,wrs("strip.tga",packed,static_cast<std::uint32_t>(strip.size())));
    require(c4::assets::load_cubemap(archive.string()+"::strip.tga").rgba==expected,
            "cubemap did not use shared original resource loading");
}
void cubemap_orientation() {
    Temp temp; const auto path=temp.dir/"grid.tga";
    // Original acknex.dll 1002f980 maps D3D faces to strip 2,0,5,4,1,3
    // using unchanged source rectangles, not rotations or reflected rows.
    constexpr std::array<std::uint8_t,6> source_face{2,0,5,4,1,3};
    Bytes expected;
    for (const auto face : source_face)
        for (std::uint8_t y=0; y<2; ++y)
            for (std::uint8_t x=0; x<2; ++x)
                expected.insert(expected.end(),{face,x,y,static_cast<std::uint8_t>(31+face*2+x+y)});
    for (const bool top_left : {true,false}) {
        save(path,cube_tga(12,2,top_left));
        require(c4::assets::load_cubemap(path).rgba==expected,
                "cubemap source face order, rotation or decoded row origin mismatch");
        require(c4::assets::cubemap_from_image(c4::assets::load_image(path)).rgba==expected,
                "direct cubemap conversion changed canonical strip face pixels");
    }
    // Power-of-two 1x1 faces are also valid.
    save(path,cube_tga(6,1));
    const auto unit=c4::assets::load_cubemap(path);
    require(unit.width==1 && unit.height==1 && unit.face_count==6 && unit.rgba.size()==24,
            "single-pixel cubemap rejected");
    require(c4::assets::cubemap_from_image(c4::assets::load_image(path)).rgba==unit.rgba,
            "direct single-pixel cubemap differs from path conversion");
    save(path,cube_tga(12,2));
    auto mip_strip=c4::assets::load_image(path);
    mip_strip.mip_levels.push_back({6,1,0});
    for (std::uint8_t face=0; face<6; ++face)
        mip_strip.mip_rgba.insert(mip_strip.mip_rgba.end(),{face,11,22,static_cast<std::uint8_t>(31+face)});
    const auto mip_cube=c4::assets::cubemap_from_image(mip_strip);
    require(mip_cube.rgba==expected && mip_cube.mip_levels.size()==1 &&
            mip_cube.mip_levels[0].width==1 && mip_cube.mip_levels[0].height==1 &&
            mip_cube.mip_levels[0].offset==0 && mip_cube.mip_rgba.size()==24,
            "cached strip lower mip lost six-plane topology or changed base pixels");
    for (std::size_t face=0; face<6; ++face)
        require(Bytes(mip_cube.mip_rgba.begin()+face*4,mip_cube.mip_rgba.begin()+face*4+4)==
                    Bytes({source_face[face],11,22,static_cast<std::uint8_t>(31+source_face[face])}),
                "cubemap stored lower mip lost binary-derived face order or alpha");
    mip_strip.mip_rgba.pop_back();
    rejects([&]{ c4::assets::cubemap_from_image(mip_strip); });
    mip_strip.mip_rgba.push_back(0); mip_strip.mip_levels[0].offset=1;
    rejects([&]{ c4::assets::cubemap_from_image(mip_strip); });
    mip_strip.mip_levels[0]={5,1,0};
    rejects([&]{ c4::assets::cubemap_from_image(mip_strip); });
}
void original_cubemaps(const std::filesystem::path& archive) {
    for (const unsigned index : {0,1,3,4,6,7,10,11}) {
        const auto path=archive.string()+"::skycube"+std::to_string(index)+"_128+6.tga";
        const auto cube=c4::assets::load_cubemap(path);
        require(cube.width==128 && cube.height==128 && cube.face_count==6 && cube.rgba.size()==393216 &&
                cube.source_format==c4::assets::ImageSourceFormat::Tga && cube.source_bits_per_pixel==24,
                "original selected cubemap dimensions, six-face payload or metadata mismatch");
        if (index!=0) continue;
        // Independent WRS3/LZSS + Pillow TGA samples from cubemap-image-evidence.json,
        // reordered by the original DLL: TL,TR,BL,BR for each canonical D3D face.
        constexpr std::array<std::array<std::array<std::uint8_t,4>,4>,6> corners{{
            {{{249,255,255,255},{106,153,185,255},{44,76,93,255},{10,38,55,255}}},
            {{{131,173,202,255},{253,255,255,255},{39,65,75,255},{35,47,47,255}}},
            {{{124,165,192,255},{118,162,192,255},{233,255,255,255},{247,255,255,255}}},
            {{{44,59,62,255},{75,104,110,255},{32,47,50,255},{52,76,86,255}}},
            {{{253,255,255,255},{250,255,255,255},{29,40,40,255},{37,70,86,255}}},
            {{{107,154,185,255},{124,165,191,255},{8,33,49,255},{26,61,70,255}}}
        }};
        for (std::size_t face=0; face<6; ++face)
            for (std::size_t corner=0; corner<4; ++corner) {
                const auto pixel=face*128*128+(corner/2)*127*128+(corner%2)*127;
                for (std::size_t channel=0; channel<4; ++channel)
                    require(cube.rgba[pixel*4+channel]==corners[face][corner][channel],
                            "original cubemap pixel differs from independently decoded canonical corner");
            }
    }
}
void cubemap_errors() {
    Temp temp; const auto path=temp.dir/"strip.tga";
    // A strip must have six horizontal, square, power-of-two faces.
    for (const auto dimensions : {std::array<std::uint16_t,2>{14,2}, {12,3}, {18,3}, {2,12}, {0,2}, {12,0}}) {
        save(path,cube_tga(dimensions[0],dimensions[1]));
        rejects([&]{ c4::assets::load_cubemap(path); });
    }
    auto strip=cube_tga(12,2);
    for (std::size_t length=0; length<strip.size(); ++length) {
        save(path,Bytes(strip.begin(),strip.begin()+length));
        rejects([&]{ c4::assets::load_cubemap(path); });
    }
    strip[12]=255; strip[13]=255; save(path,strip);
    rejects([&]{ c4::assets::load_cubemap(path); });
    const auto dds_path=temp.dir/"cube.dds";
    Bytes dds(128); put32(dds,0,0x20534444); put32(dds,4,124);
    put32(dds,12,2); put32(dds,16,2); put32(dds,76,32);
    put32(dds,108,0x1000); put32(dds,112,0xfe00);
    save(dds_path,dds);
    rejects([&]{ c4::assets::load_cubemap(dds_path); }); // Existing 2D DDS rejection remains in force.
    const auto direct_rejects = [](const c4::assets::Image& image, const char* message) {
        try { c4::assets::cubemap_from_image(image); }
        catch (const std::runtime_error& error) {
            require(std::string(error.what())==message, "direct cubemap rejected for the wrong reason");
            return;
        }
        require(false,"malformed direct cubemap accepted or replaced by fallback");
    };
    constexpr auto dimensions_error="image: dimensions exceed 16384 per axis or 64M pixels, or are zero";
    for (const auto dimensions : {std::array<std::uint32_t,2>{0,2}, {12,0}, {16385,1},
                                  {1,16385}, {16384,8192}, {0xffffffffu,0xffffffffu}})
        direct_rejects({dimensions[0],dimensions[1],{}},dimensions_error);
    constexpr auto faces_error="image: cubemap requires six horizontal square power-of-two faces";
    for (const auto dimensions : {std::array<std::uint32_t,2>{14,2}, {12,3}, {18,3}, {2,12}})
        direct_rejects({dimensions[0],dimensions[1],Bytes(std::size_t(dimensions[0])*dimensions[1]*4)},
                       faces_error);
    c4::assets::Image direct{12,2,Bytes(12*2*4)};
    for (const std::uint32_t faces : {0u,2u,6u}) {
        direct.face_count=faces;
        direct_rejects(direct,faces_error);
    }
    direct.face_count=1;
    direct.rgba.pop_back();
    direct_rejects(direct,"image: cubemap pixel payload size mismatch");
    direct.rgba.resize(12*2*4+1);
    direct_rejects(direct,"image: cubemap pixel payload size mismatch");
}

Bytes gpu_dds_fixture(std::uint32_t fourcc, std::size_t block_bytes) {
    Bytes bytes(128);
    put32(bytes,0,0x20534444); put32(bytes,4,124); put32(bytes,8,0xa1007);
    put32(bytes,12,8); put32(bytes,16,8); put32(bytes,20,4*block_bytes);
    put32(bytes,28,4); put32(bytes,76,32); put32(bytes,80,4);
    put32(bytes,84,fourcc); put32(bytes,108,0x401008);
    for (std::size_t n=0; n<7*block_bytes; ++n)
        bytes.push_back(static_cast<std::uint8_t>(n*37+11));
    return bytes;
}

std::uint16_t packed_pixel(const c4::assets::GpuImage& image, std::size_t pixel) {
    const auto offset=image.levels.at(0).offset+pixel*2;
    return std::uint16_t(image.texels.at(offset)) |
           (std::uint16_t(image.texels.at(offset+1))<<8);
}

void require_gpu_level(const c4::assets::GpuImageLevel& level, std::uint32_t width,
                       std::uint32_t height, std::size_t offset, std::size_t bytes,
                       std::size_t pitch, std::size_t face_stride) {
    require(level.width==width && level.height==height && level.offset==offset &&
            level.byte_size==bytes && level.row_pitch==pitch && level.face_stride==face_stride,
            "GPU level byte range/pitch/face stride mismatch");
}

void gpu_dds_behavior() {
    using namespace c4::assets;
    Temp temp; const auto path=temp.dir/"blocks.dds";
    constexpr std::array formats{GpuImageFormat::Bc1Unorm,GpuImageFormat::Bc2Unorm,
                                 GpuImageFormat::Bc3Unorm};
    constexpr std::array<std::uint32_t,3> fourcc{0x31545844,0x33545844,0x35545844};
    for (std::size_t format=0; format<formats.size(); ++format) {
        const std::size_t block_bytes=format==0 ? 8 : 16;
        const auto source=gpu_dds_fixture(fourcc[format],block_bytes);
        save(path,source);
        const auto gpu=load_gpu_image(path);
        require(gpu.texels==source && read_resource(path)==source,
                "GPU DDS did not retain actual original header and encoded payload");
        require(gpu.format==formats[format] && gpu.width==8 && gpu.height==8 &&
                gpu.face_count==1 && gpu.levels.size()==4 &&
                gpu.source_format==ImageSourceFormat::Dds && gpu.source_bits_per_pixel==0 &&
                gpu.source_alpha_known && gpu.source_alpha_flag==(format!=0) &&
                !gpu.implementation_precision_fallback && !gpu.color_key_applied,
                "GPU BC format/source alpha metadata mismatch");
        Image diagnostic{1,1,{1,2,3,4}};
        const auto paired=load_gpu_image(path,diagnostic,{true});
        const auto expected_diagnostic=load_image(path);
        require(paired.texels==source && paired.format==gpu.format &&
                paired.levels.size()==gpu.levels.size() &&
                paired.levels.front().offset==128 && !paired.color_key_applied &&
                !paired.implementation_precision_fallback &&
                paired.source_alpha_known && paired.source_alpha_flag==(format!=0),
                "requested CPU DDS diagnostic replaced native GPU blocks or source metadata");
        require(diagnostic.width==8 && diagnostic.height==8 && diagnostic.face_count==1 &&
                diagnostic.rgba==expected_diagnostic.rgba &&
                diagnostic.mip_rgba==expected_diagnostic.mip_rgba &&
                diagnostic.mip_levels.size()==3 &&
                diagnostic.source_format==ImageSourceFormat::Dds &&
                diagnostic.source_bits_per_pixel==0 && diagnostic.source_alpha_known &&
                diagnostic.source_alpha_flag==(format!=0),
                "paired DDS loader lost explicit CPU diagnostic bytes or original source flags");
        for (std::size_t lod=0; lod<diagnostic.mip_levels.size(); ++lod)
            require(diagnostic.mip_levels[lod].width==expected_diagnostic.mip_levels[lod].width &&
                    diagnostic.mip_levels[lod].height==expected_diagnostic.mip_levels[lod].height &&
                    diagnostic.mip_levels[lod].offset==expected_diagnostic.mip_levels[lod].offset,
                    "paired DDS loader changed stored CPU diagnostic mip layout");
        Bytes literal;
        for (std::size_t offset=0; offset<source.size(); offset+=8) {
            literal.push_back(0xff);
            const auto count=std::min<std::size_t>(8,source.size()-offset);
            literal.insert(literal.end(),source.begin()+offset,source.begin()+offset+count);
        }
        const auto archive=temp.dir/"blocks.rc4";
        save(archive,wrs("blocks.dds",literal,static_cast<std::uint32_t>(source.size())));
        const auto resource=archive.string()+"::blocks.dds";
        require(read_resource(resource)==source && load_gpu_image(resource).texels==source,
                "GPU DDS archive loading changed original bytes or header offsets");
        std::size_t offset=128;
        for (std::size_t lod=0; lod<4; ++lod) {
            const auto dimension=8u>>lod;
            const auto blocks=(dimension+3)/4;
            const auto bytes=blocks*blocks*block_bytes;
            require_gpu_level(gpu.levels[lod],dimension,dimension,offset,bytes,
                              blocks*block_bytes,bytes);
            offset+=bytes;
        }
        const auto overlay=load_gpu_image(path,{true});
        require(overlay.texels==source && overlay.format==gpu.format &&
                !overlay.color_key_applied && overlay.source_alpha_flag==gpu.source_alpha_flag,
                "GPU DDS overlay decoded blocks or applied an RGB key");
        require(decode_gpu_image(gpu).rgba==load_image(path).rgba,
                "explicit GPU DDS diagnostic differs from shared CPU decoder");
        for (std::size_t length=0; length<source.size(); ++length) {
            save(path,Bytes(source.begin(),source.begin()+length));
            rejects([&]{ load_gpu_image(path); });
        }
        auto invalid=source; put32(invalid,28,5); save(path,invalid);
        rejects([&]{ load_gpu_image(path); });
        invalid=source; put32(invalid,12,16385); save(path,invalid);
        rejects([&]{ load_gpu_image(path); });
        invalid=source; put32(invalid,112,0xfe00); save(path,invalid);
        rejects([&]{ load_gpu_image(path); });
        auto bad_range=gpu;
        bad_range.levels.front().offset=std::numeric_limits<std::size_t>::max();
        rejects([&]{ decode_gpu_image(bad_range); });
        bad_range=gpu; bad_range.levels.front().byte_size=std::numeric_limits<std::size_t>::max();
        rejects([&]{ decode_gpu_image(bad_range); });
    }
    // Header-only fixtures reject aggregate decoded sizes without giant allocations.
    Bytes overbudget(128); put32(overbudget,0,0x20534444); put32(overbudget,4,124);
    put32(overbudget,8,0x2100f); put32(overbudget,12,8192); put32(overbudget,16,8192);
    put32(overbudget,20,8192*4); put32(overbudget,28,14); put32(overbudget,76,32);
    put32(overbudget,80,0x41); put32(overbudget,88,32); put32(overbudget,92,0xff0000);
    put32(overbudget,96,0xff00); put32(overbudget,100,0xff); put32(overbudget,104,0xff000000);
    put32(overbudget,108,0x401008); save(path,overbudget);
    rejects([&]{ load_gpu_image(path); });
}

void gpu_dds_alpha_behavior() {
    using namespace c4::assets;
    Temp temp; const auto path=temp.dir/"masked.dds";
    // Only masks corresponding to proven original D3D formats establish the
    // original 100 flag. The last layout has alpha pixels, but no proven format.
    constexpr std::array<std::array<std::uint32_t,7>,7> layouts{{
        {32,0xff0000,0xff00,0xff,0xff000000,1,1}, // A8R8G8B8, 21.
        {32,0xff0000,0xff00,0xff,0,1,0},          // X8R8G8B8, 22.
        {32,0xff,0xff00,0xff0000,0xff000000,1,0}, // A8B8G8R8, 32: not flag 100.
        {16,0xf800,0x7e0,0x1f,0,1,0},           // R5G6B5, 23.
        {16,0x7c00,0x3e0,0x1f,0x8000,1,0},     // A1R5G5B5, 25.
        {16,0xf00,0xf0,0xf,0xf000,1,1},         // A4R4G4B4, 26.
        {32,0x3ff,0xffc00,0x3ff00000,0xc0000000,0,0}
    }};
    for (const auto& layout : layouts) {
        Bytes bytes(128); put32(bytes,0,0x20534444); put32(bytes,4,124);
        put32(bytes,8,0x100f); put32(bytes,12,1); put32(bytes,16,2);
        put32(bytes,20,layout[0]/4); put32(bytes,76,32);
        put32(bytes,80,layout[4] ? 0x41 : 0x40); put32(bytes,88,layout[0]);
        for (std::size_t mask=0; mask<4; ++mask) put32(bytes,92+mask*4,layout[mask+1]);
        put32(bytes,108,0x1000); bytes.insert(bytes.end(),layout[0]/4,0xff); save(path,bytes);
        const auto cpu=load_image(path); const auto gpu=load_gpu_image(path);
        require(cpu.source_alpha_known==bool(layout[5]) && cpu.source_alpha_flag==bool(layout[6]) &&
                gpu.source_alpha_known==cpu.source_alpha_known &&
                gpu.source_alpha_flag==cpu.source_alpha_flag,
                "DDS source alpha used pixel alpha/bpp instead of proven original D3D format");
    }
}

Bytes gpu_bmp_fixture(bool indexed) {
    const std::size_t offset=indexed ? 54+256*4 : 54;
    Bytes bytes(offset); bytes[0]='B'; bytes[1]='M';
    put32(bytes,2,static_cast<std::uint32_t>(offset+12)); put32(bytes,10,offset);
    put32(bytes,14,40); put32(bytes,18,3); put32(bytes,22,1);
    bytes[26]=1; bytes[28]=indexed ? 8 : 24;
    put32(bytes,34,indexed ? 4 : 12);
    if (indexed) {
        put32(bytes,46,256);
        bytes[54+4]=16; bytes[54+5]=120; bytes[54+6]=240;
        bytes[54+8]=23; bytes[54+9]=122; bytes[54+10]=247;
        bytes[54+12]=16; bytes[54+13]=120; bytes[54+14]=248;
        bytes.insert(bytes.end(),{1,2,3,0});
        put32(bytes,2,static_cast<std::uint32_t>(bytes.size()));
    } else bytes.insert(bytes.end(),{16,120,240,23,122,247,16,120,248,0,0,0});
    return bytes;
}

void gpu_bmp_behavior() {
    using namespace c4::assets;
    Temp temp; const auto path=temp.dir/"quantized.bmp";
    for (const bool indexed : {false,true}) {
        const auto source=gpu_bmp_fixture(indexed); save(path,source);
        const auto raw=load_image(path);
        const auto gpu=load_gpu_image(path);
        require(gpu.format==GpuImageFormat::Rgb565Unorm &&
                gpu.source_format==ImageSourceFormat::Bmp &&
                gpu.source_bits_per_pixel==(indexed ? 8 : 24) &&
                gpu.source_alpha_known && !gpu.source_alpha_flag &&
                !gpu.color_key_applied && !gpu.implementation_precision_fallback,
                "GPU BMP original packed upload metadata mismatch");
        require(packed_pixel(gpu,0)==(indexed ? 0x13de : 0xf3c2) &&
                packed_pixel(gpu,1)==packed_pixel(gpu,0) &&
                packed_pixel(gpu,2)!=packed_pixel(gpu,0),
                "GPU BMP original nonstandard quantizer or palette channel order mismatch");
        require_gpu_level(gpu.levels.at(0),3,1,0,6,6,6);
        const auto overlay=load_gpu_image(path,{true});
        require(overlay.format==GpuImageFormat::Argb1555Unorm && overlay.color_key_applied &&
                overlay.source_alpha_known && !overlay.source_alpha_flag &&
                !overlay.implementation_precision_fallback,
                "GPU BMP overlay promoted source alpha flag or lost typed target");
        const auto borrowed=gpu_image_from_image(raw);
        const auto borrowed_overlay=gpu_image_from_image(raw,{true});
        require(borrowed.texels==gpu.texels && borrowed.format==gpu.format &&
                borrowed_overlay.texels==overlay.texels && borrowed_overlay.format==overlay.format &&
                borrowed_overlay.source_alpha_known && !borrowed_overlay.source_alpha_flag,
                "borrowed original BMP24/palette8 upload differs from resource-derived role conversion");
        Image diagnostic{1,1,{1,2,3,4}};
        const auto paired=load_gpu_image(path,diagnostic,{true});
        require(paired.texels==overlay.texels && paired.format==overlay.format &&
                paired.color_key_applied && paired.source_alpha_known && !paired.source_alpha_flag &&
                !paired.implementation_precision_fallback,
                "paired BMP loader differs from original first-upload overlay conversion");
        require(diagnostic.width==raw.width && diagnostic.height==raw.height &&
                diagnostic.face_count==raw.face_count && diagnostic.rgba==raw.rgba &&
                diagnostic.mip_rgba.empty() && diagnostic.mip_levels.empty() &&
                diagnostic.source_format==raw.source_format &&
                diagnostic.source_bits_per_pixel==raw.source_bits_per_pixel &&
                diagnostic.source_alpha_known && !diagnostic.source_alpha_flag &&
                diagnostic.rgba[3]==255 && diagnostic.rgba[7]==255 &&
                read_resource(path)==source,
                "paired BMP overlay quantized/keyed CPU diagnostic or modified original bytes");
        require(packed_pixel(overlay,0)==(indexed ? 0x09fe : 0x79e2) &&
                packed_pixel(overlay,1)==packed_pixel(overlay,0) &&
                (packed_pixel(overlay,2)&0x8000)!=0,
                "GPU BMP key was not matched in first target 1555 quantized space");
        require((packed_pixel(overlay,0)|0x8000)==(indexed ? 0x89fe : 0xf9e2),
                "GPU BMP pre-key original target 1555 conversion mismatch");
        const auto decoded=decode_gpu_image(overlay);
        require(decoded.rgba[3]==0 && decoded.rgba[7]==0 && decoded.rgba[11]==255,
                "explicit packed GPU diagnostic lost one-bit target alpha");
        require(raw.rgba==load_image(path).rgba && read_resource(path)==source &&
                raw.rgba==Bytes({240,120,16,255,247,122,23,255,248,120,16,255}),
                "GPU BMP upload modified raw resource or CPU source pixels");
        const auto rgba=load_gpu_image(path,{false,32,32});
        require(rgba.format==GpuImageFormat::Rgba8Unorm && rgba.texels==raw.rgba,
                "BMP ignored caller's actual first-upload texture depth");
    }
    Image chain{4,2,Bytes(32)};
    chain.source_format=ImageSourceFormat::Bmp; chain.source_bits_per_pixel=24;
    chain.source_alpha_known=true;
    for (std::size_t pixel=0; pixel<8; ++pixel) {
        chain.rgba[pixel*4]=240; chain.rgba[pixel*4+1]=120;
        chain.rgba[pixel*4+2]=16; chain.rgba[pixel*4+3]=255;
    }
    chain.mip_levels={{2,1,0},{1,1,8}};
    chain.mip_rgba={248,120,16,255,247,122,23,255,247,122,23,255};
    const auto before=chain;
    const auto* base_storage=chain.rgba.data();
    const auto* lower_storage=chain.mip_rgba.data();
    const auto* level_storage=chain.mip_levels.data();
    const auto base_capacity=chain.rgba.capacity();
    const auto lower_capacity=chain.mip_rgba.capacity();
    const auto level_capacity=chain.mip_levels.capacity();
    const auto borrowed=gpu_image_from_image(static_cast<const Image&>(chain),{true});
    const auto borrowed_rgba=gpu_image_from_image(static_cast<const Image&>(chain),{true,32,32});
    require(borrowed.format==GpuImageFormat::Argb1555Unorm && borrowed.color_key_applied &&
            borrowed.source_alpha_known && !borrowed.source_alpha_flag &&
            packed_pixel(borrowed,0)==0x79e2 && borrowed.levels.size()==3 &&
            borrowed.texels.size()==22 && (borrowed.texels[17]&0x80)!=0 &&
            (borrowed.texels[19]&0x80)==0 && (borrowed.texels[21]&0x80)==0,
            "borrowed BMP16 upload differs from exact packed base-key and stored mip conversion");
    require(borrowed_rgba.format==GpuImageFormat::Rgba8Unorm &&
            borrowed_rgba.color_key_applied && borrowed_rgba.levels.size()==3 &&
            borrowed_rgba.texels.size()==44 && borrowed_rgba.texels.data()!=base_storage &&
            borrowed_rgba.source_alpha_known && !borrowed_rgba.source_alpha_flag,
            "borrowed RGBA8 upload did not create independent owned base-plus-mip storage");
    require_gpu_level(borrowed_rgba.levels[0],4,2,0,32,16,32);
    require_gpu_level(borrowed_rgba.levels[1],2,1,32,8,8,8);
    require_gpu_level(borrowed_rgba.levels[2],1,1,40,4,4,4);
    for (std::size_t pixel=0; pixel<8; ++pixel)
        require(borrowed_rgba.texels[pixel*4]==240 && borrowed_rgba.texels[pixel*4+1]==120 &&
                borrowed_rgba.texels[pixel*4+2]==16 && borrowed_rgba.texels[pixel*4+3]==0,
                "borrowed RGBA8 did not key exact original base RGB in owned output");
    require(Bytes(borrowed_rgba.texels.begin()+32,borrowed_rgba.texels.end())==before.mip_rgba,
            "borrowed RGBA8 incorrectly keyed near-but-distinct lower RGB after quantization");
    require(chain.width==before.width && chain.height==before.height &&
            chain.face_count==before.face_count && chain.source_format==before.source_format &&
            chain.source_bits_per_pixel==before.source_bits_per_pixel &&
            chain.source_alpha_known==before.source_alpha_known &&
            chain.source_alpha_flag==before.source_alpha_flag &&
            chain.rgba==before.rgba && chain.mip_rgba==before.mip_rgba &&
            chain.rgba.data()==base_storage && chain.mip_rgba.data()==lower_storage &&
            chain.mip_levels.data()==level_storage && chain.rgba.capacity()==base_capacity &&
            chain.mip_rgba.capacity()==lower_capacity && chain.mip_levels.capacity()==level_capacity &&
            chain.mip_levels.size()==before.mip_levels.size(),
            "borrowed GPU conversions mutated CPU bytes, metadata, storage, or capacity");
    for (std::size_t lod=0; lod<chain.mip_levels.size(); ++lod)
        require(chain.mip_levels[lod].width==before.mip_levels[lod].width &&
                chain.mip_levels[lod].height==before.mip_levels[lod].height &&
                chain.mip_levels[lod].offset==before.mip_levels[lod].offset,
                "borrowed GPU conversion mutated authored CPU level layout");
    const auto gpu=gpu_image_from_image(std::move(chain),{true});
    require(gpu.levels.size()==3,"packed upload generated or discarded authored mip levels");
    require_gpu_level(gpu.levels[1],2,1,16,4,4,4);
    require_gpu_level(gpu.levels[2],1,1,20,2,2,2);
    require((gpu.texels[17]&0x80)!=0 && (gpu.texels[19]&0x80)==0 &&
            (gpu.texels[21]&0x80)==0,
            "packed lower mip used a new per-level key instead of first base target key");
    require(gpu.texels==borrowed.texels && gpu.format==borrowed.format &&
            gpu.source_alpha_known==borrowed.source_alpha_known &&
            gpu.source_alpha_flag==borrowed.source_alpha_flag,
            "borrowed/rvalue GPU packed conversion differs");
}

void gpu_tga_behavior() {
    using namespace c4::assets;
    Temp temp; const auto path=temp.dir/"key.tga";
    Bytes source(18); source[2]=2; source[12]=2; source[14]=1;
    source[16]=24; source[17]=0x20;
    source.insert(source.end(),{3,2,1,4,2,1}); save(path,source);
    const auto raw=load_image(path);
    const auto gpu=load_gpu_image(path,{true});
    require(gpu.format==GpuImageFormat::Rgba8Unorm &&
            gpu.texels==Bytes({1,2,3,0,1,2,4,255}) && gpu.color_key_applied &&
            gpu.source_alpha_known && !gpu.source_alpha_flag &&
            !gpu.implementation_precision_fallback,
            "TGA24 did not key original RGBA pixels before filtering/quantization");
    require(raw.rgba==Bytes({1,2,3,255,1,2,4,255}) &&
            load_image(path).rgba==raw.rgba && read_resource(path)==source,
            "TGA overlay mutated raw diagnostic source");
    GpuImageUploadOptions runtime_options{true,16,32,GpuImageRuntimeContext{0x40000,8,256,0}};
    rejects([&]{
        derive_gpu_image_upload(gpu_image_from_image(raw,{true}),*runtime_options.runtime,true);
    });
    const auto runtime=gpu_image_from_image(raw,runtime_options);
    require(runtime.runtime && runtime.width==8 && runtime.height==8 &&
            runtime.levels.size()==4 && runtime.color_key_applied &&
            runtime.source_alpha_known && !runtime.source_alpha_flag &&
            runtime.texels[3]==0 && runtime.texels[7]==255 &&
            runtime.texels[runtime.levels[1].offset+3]==64 &&
            raw.rgba==Bytes({1,2,3,255,1,2,4,255}),
            "runtime overlay reused keyed upload instead of authored CPU alpha before mip generation");
    auto owned=raw; const auto* owned_storage=owned.rgba.data();
    const auto moved=gpu_image_from_image(std::move(owned),{true});
    require(moved.texels.data()==owned_storage && moved.texels==gpu.texels,
            "rvalue RGBA8 GPU upload cloned rather than transferring and keying owned base storage");
    const auto packed=load_gpu_image(path,{true,16,16});
    require(packed.format==GpuImageFormat::Argb1555Unorm && packed.color_key_applied &&
            packed_pixel(packed,0)==packed_pixel(packed,1),
            "TGA24 alpha_depth16 ignored packed target quantized key");
    source.resize(18); source[16]=32; source[17]=0x28;
    source.insert(source.end(),{3,2,1,4,3,2,1,201}); save(path,source);
    const auto authored=load_gpu_image(path,{true});
    require(authored.format==GpuImageFormat::Rgba8Unorm &&
            authored.texels==Bytes({1,2,3,4,1,2,3,201}) &&
            authored.source_alpha_known && authored.source_alpha_flag &&
            !authored.color_key_applied,
            "TGA32 original source alpha flag failed to bypass overlay RGB key");
    Image unknown{1,1,{1,2,3,4}};
    const auto unproven=gpu_image_from_image(std::move(unknown));
    require(!unproven.source_alpha_known && !unproven.source_alpha_flag,
            "unknown source alpha was guessed from decoded RGBA channels");
}

void gpu_cube_behavior() {
    using namespace c4::assets;
    Temp temp; const auto path=temp.dir/"strip.tga";
    save(path,cube_tga(12,2));
    auto strip=load_image(path);
    strip.mip_levels={{6,1,0},{3,1,24},{1,1,36}};
    for (std::uint8_t face=0; face<6; ++face)
        strip.mip_rgba.insert(strip.mip_rgba.end(),{face,11,22,static_cast<std::uint8_t>(31+face)});
    strip.mip_rgba.insert(strip.mip_rgba.end(),16,0);
    const auto cube=gpu_cubemap_from_image(gpu_image_from_image(std::move(strip)));
    require(cube.width==2 && cube.height==2 && cube.face_count==6 &&
            cube.format==GpuImageFormat::Rgba8Unorm && cube.levels.size()==2 &&
            cube.texels.size()==120 && cube.source_format==ImageSourceFormat::Tga &&
            cube.source_bits_per_pixel==32 && cube.source_alpha_known && cube.source_alpha_flag &&
            !cube.implementation_precision_fallback && !cube.color_key_applied,
            "GPU cube changed source metadata or retained invalid face LODs");
    require_gpu_level(cube.levels[0],2,2,0,96,8,16);
    require_gpu_level(cube.levels[1],1,1,96,24,4,4);
    constexpr std::array<std::uint8_t,6> source_faces{2,0,5,4,1,3};
    for (std::size_t face=0; face<6; ++face) {
        for (std::size_t y=0; y<2; ++y)
            for (std::size_t x=0; x<2; ++x) {
                const auto offset=face*16+(y*2+x)*4;
                require(cube.texels[offset]==source_faces[face] &&
                        cube.texels[offset+1]==x && cube.texels[offset+2]==y &&
                        cube.texels[offset+3]==31+source_faces[face]*2+x+y,
                        "GPU cube changed canonical face order or row-major orientation");
            }
        const auto offset=96+face*4;
        require(cube.texels[offset]==source_faces[face] && cube.texels[offset+1]==11 &&
                cube.texels[offset+2]==22 && cube.texels[offset+3]==31+source_faces[face],
                "GPU cube lower level used wrong strip face or alpha");
    }
    const auto diagnostic=decode_gpu_image(cube);
    require(diagnostic.face_count==6 && diagnostic.rgba.size()==96 &&
            diagnostic.mip_levels.size()==1 && diagnostic.mip_rgba.size()==24,
            "explicit GPU cube diagnostic lost face or lower mip topology");
    for (const auto dimensions : {std::array<std::uint16_t,2>{14,2},{12,3},{18,3},{2,12}}) {
        save(path,cube_tga(dimensions[0],dimensions[1]));
        rejects([&]{ gpu_cubemap_from_image(load_gpu_image(path)); });
    }
    save(path,cube_tga(12,2));
    auto malformed=load_gpu_image(path); malformed.texels.pop_back();
    rejects([&]{ gpu_cubemap_from_image(std::move(malformed)); });
    malformed=load_gpu_image(path); malformed.levels.front().row_pitch=47;
    rejects([&]{ gpu_cubemap_from_image(std::move(malformed)); });
    malformed=load_gpu_image(path); malformed.face_count=6;
    rejects([&]{ gpu_cubemap_from_image(std::move(malformed)); });
    auto invalid=load_image(path); invalid.mip_levels={{5,1,0}}; invalid.mip_rgba.resize(20);
    rejects([&]{ gpu_cubemap_from_image(gpu_image_from_image(std::move(invalid))); });
    // Compressed strips are an explicit decoded precision fallback, not invented
    // source block rearrangement or an exact compressed cube upload.
    const auto dds_path=temp.dir/"strip.dds";
    auto blocks=gpu_dds_fixture(0x31545844,8); blocks.resize(128+48);
    put32(blocks,8,0x81007); put32(blocks,12,4); put32(blocks,16,24);
    put32(blocks,20,48); put32(blocks,28,0); put32(blocks,108,0x1000); save(dds_path,blocks);
    const auto fallback=gpu_cubemap_from_image(load_gpu_image(dds_path));
    require(fallback.format==GpuImageFormat::Rgba8Unorm && fallback.face_count==6 &&
            fallback.width==4 && fallback.height==4 && fallback.levels.size()==1 &&
            fallback.implementation_precision_fallback && fallback.source_alpha_known &&
            !fallback.source_alpha_flag && fallback.texels==load_cubemap(dds_path).rgba,
            "compressed cube strip fallback claimed native precision or changed canonical pixels");
}

void gpu_sky_faces_behavior() {
    using namespace c4::assets;
    Temp temp; const auto path=temp.dir/"sky+6.tga";
    for (const bool top_left : {false,true}) {
        save(path,cube_tga(12,2,top_left));
        const auto strip=load_image(path);
        const auto original=strip.rgba;
        const auto faces=gpu_sky_faces_from_image(strip);
        for (std::size_t face=0; face<faces.size(); ++face) {
            const auto& gpu=faces[face];
            require(gpu.width==2 && gpu.height==2 && gpu.face_count==1 &&
                    gpu.format==GpuImageFormat::Rgba8Unorm && gpu.levels.size()==1 &&
                    gpu.source_format==ImageSourceFormat::Tga && gpu.source_bits_per_pixel==32 &&
                    gpu.source_alpha_known && gpu.source_alpha_flag &&
                    !gpu.runtime && !gpu.color_key_applied && !gpu.implementation_precision_fallback,
                    "declared sky face lost source alpha/depth or acquired cube/upload state");
            require_gpu_level(gpu.levels[0],2,2,0,16,8,16);
            for (std::size_t y=0; y<2; ++y) for (std::size_t x=0; x<2; ++x)
                require(Bytes(gpu.texels.begin()+(y*2+x)*4,gpu.texels.begin()+(y*2+x+1)*4)==
                        Bytes({static_cast<std::uint8_t>(face),static_cast<std::uint8_t>(x),
                               static_cast<std::uint8_t>(y),static_cast<std::uint8_t>(31+face*2+x+y)}),
                        "declared sky split reordered, rotated or reflected source strip pixels");
        }
        require(strip.rgba==original,"sky split mutated its CPU source");
        GpuImageUploadOptions options{true,16,32,GpuImageRuntimeContext{0x40000,8,256,0}};
        const auto uploaded=gpu_sky_faces_from_image(strip,options);
        for (std::size_t face=0; face<uploaded.size(); ++face) {
            const auto& gpu=uploaded[face];
            require(gpu.width==8 && gpu.height==8 && gpu.face_count==1 &&
                    gpu.runtime && gpu.runtime->source_width==2 && gpu.runtime->source_height==2 &&
                    gpu.runtime->nominal_width==8 && gpu.runtime->nominal_height==8 &&
                    gpu.runtime->written_width==2 && gpu.runtime->written_height==2 &&
                    gpu.runtime->authored_mip_levels==1 && gpu.runtime->preserved_mip_levels==1 &&
                    gpu.runtime->generated_mip_levels==3 && gpu.levels.size()==4 &&
                    gpu.source_alpha_flag && !gpu.color_key_applied &&
                    gpu.texels[0]==face && gpu.texels[3]==31+face*2,
                    "sky uploaded/resized the entire strip instead of each original face");
        }
        require(options.runtime->bitmap_flags==0x40000 && strip.rgba==original,
                "sky construction changed caller runtime context or source pixels");
    }
    Image raw{12,2,Bytes(12*2*4,255)};
    raw.source_format=ImageSourceFormat::Tga; raw.source_bits_per_pixel=24;
    raw.source_alpha_known=true;
    for (unsigned y=0; y<2; ++y) for (unsigned x=0; x<12; ++x)
        raw.rgba[(y*12+x)*4]=static_cast<std::uint8_t>(x/2+16);
    const auto keyed=gpu_sky_faces_from_image(raw,{true});
    const auto packed=gpu_sky_faces_from_image(raw,{false,16,16});
    for (std::size_t face=0; face<6; ++face) {
        require(keyed[face].source_alpha_known && !keyed[face].source_alpha_flag &&
                keyed[face].color_key_applied && keyed[face].texels[3]==0 &&
                keyed[face].texels[15]==0,
                "non-alpha sky face used another face's upper-left upload key");
        require(packed[face].format==GpuImageFormat::Rgb565Unorm &&
                !packed[face].color_key_applied && packed[face].source_bits_per_pixel==24 &&
                packed[face].source_alpha_known && !packed[face].source_alpha_flag,
                "sky face bypassed ordinary authored-depth upload conversion");
    }
    // 1005fe90 divides the strip width by six without a square/POT check.
    raw.width=19; raw.height=2; raw.rgba.resize(19*2*4,73);
    const auto rectangular=gpu_sky_faces_from_image(raw);
    require(rectangular[5].width==3 && rectangular[5].height==2 &&
            rectangular[5].texels.size()==24,"sky split imposed reflection-cubemap dimensions");
    raw.rgba.pop_back(); rejects([&]{ gpu_sky_faces_from_image(raw); });
    raw.rgba.push_back(73); raw.source_format=ImageSourceFormat::Dds;
    rejects([&]{ gpu_sky_faces_from_image(raw); });
    raw.source_format=ImageSourceFormat::Tga; raw.mip_levels={{9,1,0}}; raw.mip_rgba.resize(36);
    rejects([&]{ gpu_sky_faces_from_image(raw); });
    raw.mip_levels.clear(); raw.mip_rgba.clear(); raw.width=5; raw.rgba.resize(5*2*4);
    rejects([&]{ gpu_sky_faces_from_image(raw); });
}

void gpu_skin_behavior() {
    using namespace c4::assets;
    Skin skin; skin.format=2; skin.flags=10; skin.width=8; skin.height=8;
    for (std::size_t pixel=0; pixel<85; ++pixel) {
        skin.payload.push_back(static_cast<std::uint8_t>(pixel));
        skin.payload.push_back(static_cast<std::uint8_t>(pixel+128));
    }
    skin.payload[128]=0; skin.payload[129]=0x80;
    skin.payload[160]=0; skin.payload[161]=0x80;
    const auto source=skin.payload; const auto* storage=skin.payload.data();
    const auto gpu=gpu_image_from_skin(std::move(skin));
    require(gpu.format==GpuImageFormat::Rgb565Unorm && gpu.texels==source &&
            gpu.texels.data()==storage && gpu.width==8 && gpu.height==8 &&
            gpu.levels.size()==4 && gpu.source_format==ImageSourceFormat::ModelSkin &&
            gpu.source_bits_per_pixel==16 && !gpu.implementation_precision_fallback,
            "GPU Skin2 copied/decoded original RGB565 bytes or lost source metadata");
    require(gpu.source_alpha_known && !gpu.source_alpha_flag,
            "raw Skin2 original initializer known-false alpha flag mismatch");
    require_gpu_level(gpu.levels[0],8,8,0,128,16,128);
    require_gpu_level(gpu.levels[1],4,4,128,32,8,32);
    require_gpu_level(gpu.levels[2],2,2,160,8,4,8);
    require_gpu_level(gpu.levels[3],1,1,168,2,2,2);
    Skin overlay_skin; overlay_skin.format=2; overlay_skin.flags=10;
    overlay_skin.width=8; overlay_skin.height=8; overlay_skin.payload=source;
    const auto overlay=gpu_image_from_skin(std::move(overlay_skin),{true});
    require(overlay.format==GpuImageFormat::Argb1555Unorm && overlay.color_key_applied &&
            overlay.source_alpha_known && !overlay.source_alpha_flag &&
            packed_pixel(overlay,0)==0x4000 && packed_pixel(overlay,1)==0xc081 &&
            overlay.texels[129]==0x40 && overlay.texels[161]==0x40,
            "raw Skin2 overlay failed target packed key across stored mips or promoted source alpha");
    Skin borrowed_skin; borrowed_skin.format=2; borrowed_skin.flags=0x3a;
    borrowed_skin.width=8; borrowed_skin.height=8; borrowed_skin.payload=source;
    borrowed_skin.name="embedded packed"; borrowed_skin.material={0.25f,0.5f,0.75f};
    borrowed_skin.effect="original authored effect";
    const auto borrowed_before=borrowed_skin;
    const auto* borrowed_storage=borrowed_skin.payload.data();
    const auto borrowed_capacity=borrowed_skin.payload.capacity();
    const auto* material_storage=borrowed_skin.material.data();
    const auto material_capacity=borrowed_skin.material.capacity();
    const auto borrowed=gpu_image_from_skin(static_cast<const Skin&>(borrowed_skin));
    const auto borrowed_overlay=gpu_image_from_skin(static_cast<const Skin&>(borrowed_skin),{true});
    require(borrowed.format==gpu.format && borrowed.texels==gpu.texels &&
            borrowed.texels.data()!=borrowed_storage && borrowed.levels.size()==gpu.levels.size() &&
            borrowed.source_alpha_known && !borrowed.source_alpha_flag &&
            borrowed_overlay.format==overlay.format && borrowed_overlay.texels==overlay.texels &&
            borrowed_overlay.color_key_applied && borrowed_overlay.source_alpha_known &&
            !borrowed_overlay.source_alpha_flag,
            "borrowed Skin2 original packed/1555 role conversion differs from rvalue");
    require(borrowed_skin.format==borrowed_before.format &&
            borrowed_skin.flags==borrowed_before.flags &&
            borrowed_skin.width==borrowed_before.width && borrowed_skin.height==borrowed_before.height &&
            borrowed_skin.name==borrowed_before.name &&
            borrowed_skin.external_texture==borrowed_before.external_texture &&
            borrowed_skin.effect==borrowed_before.effect && borrowed_skin.material==borrowed_before.material &&
            borrowed_skin.payload==borrowed_before.payload &&
            borrowed_skin.payload.data()==borrowed_storage &&
            borrowed_skin.payload.capacity()==borrowed_capacity &&
            borrowed_skin.material.data()==material_storage &&
            borrowed_skin.material.capacity()==material_capacity,
            "borrowed Skin2 GPU upload mutated payload storage or authored skin metadata");
    Skin pixel; pixel.format=2; pixel.flags=2; pixel.width=1; pixel.height=1;
    pixel.payload={0x82,0x10};
    const auto packed=gpu_image_from_skin(std::move(pixel));
    require(decode_gpu_image(packed).rgba==Bytes({16,16,16,255}),
            "typed RGB565 diagnostic did not honestly expand native precision");
    // Original 10060e40 -> 10043cd0 sets bitmap alpha100 only for raw kinds 3/5.
    constexpr std::array<std::uint32_t,4> raw_formats{0,3,4,5};
    const std::array<Bytes,4> payloads{Bytes{1},Bytes{0x23,0x81},Bytes{3,2,1},Bytes{3,2,1,4}};
    const std::array<Bytes,4> decoded{Bytes{1,2,3,255},Bytes{17,34,51,136},
                                     Bytes{1,2,3,255},Bytes{1,2,3,4}};
    std::array<std::uint8_t,768> palette{};
    palette[3]=1; palette[4]=2; palette[5]=3;
    for (std::size_t index=0; index<raw_formats.size(); ++index) {
        Skin raw; raw.format=raw_formats[index]; raw.flags=raw.format;
        raw.width=1; raw.height=1; raw.payload=payloads[index];
        auto overlay_raw=raw;
        const bool source_alpha=raw_formats[index]==3 || raw_formats[index]==5;
        const auto* raw_storage=raw.payload.data();
        const auto raw_capacity=raw.payload.capacity();
        const auto borrowed_raw=gpu_image_from_skin(static_cast<const Skin&>(raw),{},palette);
        const auto borrowed_authored=gpu_image_from_skin(static_cast<const Skin&>(raw),{true},palette);
        require(raw.payload==payloads[index] && raw.payload.data()==raw_storage &&
                raw.payload.capacity()==raw_capacity && raw.format==raw_formats[index] &&
                raw.flags==raw_formats[index] && raw.width==1 && raw.height==1,
                "borrowed raw MDL upload consumed or altered the original CPU skin");
        const auto image=gpu_image_from_skin(std::move(raw),{},palette);
        require(image.source_alpha_known && image.source_alpha_flag==source_alpha &&
                image.source_format==ImageSourceFormat::ModelSkin &&
                decode_gpu_image(image).rgba==decoded[index],
                "raw MDL GPU image lost proven initializer alpha flag or actual channel bytes");
        const auto authored=gpu_image_from_skin(std::move(overlay_raw),{true},palette);
        auto expected=decoded[index];
        if (!source_alpha) expected[3]=0;
        require(authored.source_alpha_known && authored.source_alpha_flag==source_alpha &&
                authored.color_key_applied==!source_alpha && decode_gpu_image(authored).rgba==expected,
                "raw MDL overlay ignored original authored-alpha classifier");
        require(borrowed_raw.texels==image.texels && borrowed_raw.format==image.format &&
                borrowed_authored.texels==authored.texels && borrowed_authored.format==authored.format &&
                borrowed_authored.source_alpha_known &&
                borrowed_authored.source_alpha_flag==source_alpha &&
                borrowed_authored.color_key_applied==!source_alpha,
                "borrowed raw MDL upload lost authored alpha or differs from rvalue role conversion");
    }
    Skin embedded; embedded.format=6; embedded.flags=6; embedded.height=1;
    embedded.payload=gpu_dds_fixture(0x31545844,8);
    embedded.width=static_cast<std::uint32_t>(embedded.payload.size());
    const auto original=embedded.payload; const auto* dds_storage=embedded.payload.data();
    embedded.name="embedded DDS"; embedded.material={0.125f,0.625f};
    embedded.effect="retained DDS effect";
    const auto embedded_before=embedded;
    const auto embedded_capacity=embedded.payload.capacity();
    const auto* embedded_material=embedded.material.data();
    const auto borrowed_dds=gpu_image_from_skin(static_cast<const Skin&>(embedded),{true});
    require(borrowed_dds.format==GpuImageFormat::Bc1Unorm &&
            borrowed_dds.texels==original && borrowed_dds.texels.data()!=dds_storage &&
            borrowed_dds.levels.size()==4 && borrowed_dds.levels.front().offset==128 &&
            borrowed_dds.source_format==ImageSourceFormat::Dds &&
            borrowed_dds.source_alpha_known && !borrowed_dds.source_alpha_flag &&
            !borrowed_dds.color_key_applied && !borrowed_dds.implementation_precision_fallback,
            "borrowed Skin6 changed native DDS blocks, byte ranges, or proven alpha provenance");
    require(embedded.payload==embedded_before.payload && embedded.payload.data()==dds_storage &&
            embedded.payload.capacity()==embedded_capacity &&
            embedded.format==embedded_before.format && embedded.flags==embedded_before.flags &&
            embedded.width==embedded_before.width && embedded.height==embedded_before.height &&
            embedded.name==embedded_before.name && embedded.effect==embedded_before.effect &&
            embedded.external_texture==embedded_before.external_texture &&
            embedded.material==embedded_before.material && embedded.material.data()==embedded_material,
            "borrowed Skin6 consumed source payload or mutated authored metadata");
    const auto dds=gpu_image_from_skin(std::move(embedded));
    require(dds.format==GpuImageFormat::Bc1Unorm && dds.texels==original &&
            dds.texels.data()==dds_storage && dds.source_format==ImageSourceFormat::Dds &&
            dds.source_alpha_known && !dds.source_alpha_flag && dds.levels.size()==4 &&
            dds.levels.front().offset==128 && !dds.implementation_precision_fallback,
            "GPU Skin6 failed to move original DDS storage or preserve validated BC metadata");
    for (const int delta : {-1,1}) {
        Skin malformed; malformed.format=6; malformed.flags=6; malformed.height=1;
        malformed.payload=original;
        if (delta<0) malformed.payload.pop_back(); else malformed.payload.push_back(0);
        malformed.width=static_cast<std::uint32_t>(malformed.payload.size());
        rejects([&]{ gpu_image_from_skin(std::move(malformed)); });
    }
    Skin malformed; malformed.format=2; malformed.flags=10; malformed.width=8; malformed.height=8;
    malformed.payload=source; malformed.payload.pop_back();
    rejects([&]{ gpu_image_from_skin(std::move(malformed)); });
    Skin external; external.format=7; external.flags=7; external.external_texture="actual.dds";
    rejects([&]{ gpu_image_from_skin(std::move(external)); });
}

c4::assets::Image runtime_image(std::uint32_t width, std::uint32_t height) {
    c4::assets::Image image{width,height,Bytes(std::size_t(width)*height*4,255)};
    image.source_format=c4::assets::ImageSourceFormat::Tga;
    image.source_bits_per_pixel=32;
    image.source_alpha_known=true; image.source_alpha_flag=true;
    return image;
}

void gpu_runtime_box_behavior() {
    using namespace c4::assets;
    GpuImageUploadOptions options;
    options.runtime=GpuImageRuntimeContext{0x40000,8,256,0};
    auto source=runtime_image(8,8);
    for (std::uint32_t y=0; y<8; ++y) for (std::uint32_t x=0; x<8; ++x) {
        const auto at=(y*8+x)*4;
        source.rgba[at]=static_cast<std::uint8_t>(x+8*y);
        source.rgba[at+1]=static_cast<std::uint8_t>(255-x-8*y);
        source.rgba[at+2]=(x&1) ? 255 : 0;
        source.rgba[at+3]=(y&1) ? 255 : 0;
    }
    const auto original=source.rgba;
    const auto direct=gpu_image_from_image(source);
    const auto gpu=gpu_image_from_image(source,options);
    require(!direct.runtime && direct.levels.size()==1 && direct.texels==original,
            "default GPU view acquired runtime derivatives");
    require(source.width==8 && source.height==8 && source.rgba==original &&
            source.mip_rgba.empty() && source.mip_levels.empty(),
            "runtime upload changed CPU source pixels or topology");
    require(gpu.runtime && gpu.width==8 && gpu.height==8 && gpu.levels.size()==4 &&
            gpu.runtime->nominal_width==8 && gpu.runtime->nominal_height==8 &&
            gpu.runtime->written_width==8 && gpu.runtime->written_height==8 &&
            gpu.runtime->authored_mip_levels==1 && gpu.runtime->preserved_mip_levels==1 &&
            gpu.runtime->generated_mip_levels==3 && gpu.runtime->resize_filter==1 &&
            gpu.runtime->mip_filter==5 && gpu.runtime->original_padding_known &&
            !gpu.implementation_precision_fallback,
            "full POT BOX metadata or precision mismatch");
    require(std::equal(original.begin(),original.end(),gpu.texels.begin()),
            "full POT runtime changed authored LOD0");
    require_gpu_level(gpu.levels[0],8,8,0,256,32,256);
    require_gpu_level(gpu.levels[1],4,4,256,64,16,64);
    require_gpu_level(gpu.levels[2],2,2,320,16,8,16);
    require_gpu_level(gpu.levels[3],1,1,336,4,4,4);
    for (std::size_t lod=1; lod<4; ++lod) {
        const auto& level=gpu.levels[lod];
        for (std::uint32_t y=0; y<level.height; ++y)
            for (std::uint32_t x=0; x<level.width; ++x) {
                const unsigned red=lod==1 ? 2*x+16*y+5 : lod==2 ? 4*x+32*y+14 : 32;
                const auto at=level.offset+(y*level.width+x)*4;
                require(gpu.texels[at]==red && gpu.texels[at+1]==256-red &&
                        gpu.texels[at+2]==128 && gpu.texels[at+3]==128,
                        "POT RGBA8 recursive BOX exact channel codes mismatch");
            }
    }
    Skin skin; skin.format=2; skin.flags=2; skin.width=8; skin.height=8;
    for (unsigned y=0; y<8; ++y) for (unsigned x=0; x<8; ++x) {
        const unsigned sample=(y&1)*2+(x&1);
        u16(skin.payload,static_cast<std::uint16_t>(
            (sample==0 ? 0xf81f : 0) | (sample<<5)));
    }
    const auto packed_source=skin.payload;
    const auto rgb565=gpu_image_from_skin(skin,options);
    require(skin.payload==packed_source && rgb565.format==GpuImageFormat::Rgb565Unorm &&
            !rgb565.implementation_precision_fallback,
            "raw565 runtime lost source storage or native precision");
    for (std::size_t lod=1; lod<4; ++lod) {
        const auto& level=rgb565.levels[lod];
        for (std::size_t at=level.offset; at<level.offset+level.byte_size; at+=2)
            require(rgb565.texels[at]==0x48 && rgb565.texels[at+1]==0x40,
                    "RGB565 BOX averaged expanded bytes instead of native codes");
    }
    GpuImage argb1555;
    argb1555.width=8; argb1555.height=8; argb1555.format=GpuImageFormat::Argb1555Unorm;
    argb1555.source_alpha_known=true; argb1555.source_alpha_flag=true;
    argb1555.levels={{8,8,0,128,16,128}};
    for (unsigned y=0; y<8; ++y) for (unsigned x=0; x<8; ++x) {
        const unsigned sample=(y&1)*2+(x&1);
        u16(argb1555.texels,static_cast<std::uint16_t>(
            (sample<2 ? 0x8000 : 0) | (sample==0 ? 0x7c00 : 0) |
            (sample<<5) | (sample==2 ? 1 : 0)));
    }
    const auto alpha=derive_gpu_image_upload(std::move(argb1555),*options.runtime,true);
    require(!alpha.color_key_applied && alpha.source_alpha_flag &&
            !alpha.implementation_precision_fallback,
            "authored1555 runtime was keyed or lost native precision");
    for (std::size_t lod=1; lod<4; ++lod) {
        const auto& level=alpha.levels[lod];
        for (std::size_t at=level.offset; at<level.offset+level.byte_size; at+=2)
            require(alpha.texels[at]==0x40 && alpha.texels[at+1]==0xa0,
                    "ARGB1555 BOX half-alpha tie must round opaque at every level");
    }
    auto supplied=runtime_image(8,8);
    supplied.mip_levels={{4,4,0}}; supplied.mip_rgba=Bytes(64,73);
    const auto chain=gpu_image_from_image(supplied,options);
    require(chain.runtime->authored_mip_levels==2 && chain.runtime->preserved_mip_levels==2 &&
            chain.runtime->generated_mip_levels==2,
            "raw supplied mip suffix source/count mismatch");
    for (std::size_t at=chain.levels[1].offset; at<chain.texels.size(); ++at)
        require(chain.texels[at]==73,"generated raw suffix did not start at last supplied mip");
    options.runtime->requested_mip_levels=0; options.runtime->bitmap_flags=0;
    require(gpu_image_from_image(supplied,options).levels.size()==2,
            "zero mip request discarded supplied raw mip");
    skin.flags=10; skin.payload=Bytes((64+16+4+1)*2,0x33);
    const auto four_authored=skin.payload;
    options.runtime->bitmap_flags=0x40000;
    const auto authored=gpu_image_from_skin(skin,options);
    require(authored.texels==four_authored && authored.runtime->authored_mip_levels==4 &&
            authored.runtime->preserved_mip_levels==4 && authored.runtime->generated_mip_levels==0,
            "runtime regenerated or discarded any of four supplied raw levels");
    supplied.source_bits_per_pixel=24; supplied.source_alpha_flag=false;
    for (std::size_t at=0; at<supplied.rgba.size(); at+=4) {
        supplied.rgba[at]=10; supplied.rgba[at+1]=10; supplied.rgba[at+2]=10;
    }
    for (std::size_t at=0; at<supplied.mip_rgba.size(); at+=4) {
        supplied.mip_rgba[at]=10; supplied.mip_rgba[at+1]=10;
        supplied.mip_rgba[at+2]=10; supplied.mip_rgba[at+3]=255;
    }
    supplied.mip_rgba[0]=40; supplied.mip_rgba[1]=40; supplied.mip_rgba[2]=40;
    options.overlay=true;
    const auto keyed=gpu_image_from_image(supplied,options);
    const auto lower=keyed.levels[1].offset;
    require(keyed.texels[3]==0 && keyed.texels[lower]==40 && keyed.texels[lower+3]==255 &&
            keyed.texels[lower+4]==10 && keyed.texels[lower+7]==0 &&
            keyed.texels[keyed.levels[2].offset+3]==64,
            "runtime raw lower levels recaptured their own key or generated before keying");
}

void gpu_runtime_key_modes_behavior() {
    using namespace c4::assets;
    GpuImageUploadOptions options{true};
    options.runtime=GpuImageRuntimeContext{0,8,256,0,false};
    auto colors=runtime_image(8,8);
    colors.source_bits_per_pixel=24; colors.source_alpha_flag=false;
    colors.rgba[0]=1; colors.rgba[1]=2; colors.rgba[2]=3;
    colors.rgba[4]=colors.rgba[5]=colors.rgba[6]=0;
    colors.rgba[8]=colors.rgba[9]=0; colors.rgba[10]=1;
    const auto original_colors=colors.rgba;
    const auto black=gpu_image_from_image(colors,options);
    require(black.texels[0]==1 && black.texels[3]==255 && black.texels[7]==0 &&
            black.texels[10]==1 && black.texels[11]==255 &&
            colors.rgba==original_colors && !black.source_alpha_flag,
            "fixed API black key captured first pixel, thresholded nearblack or changed CPU source");
    Skin packed; packed.format=2; packed.flags=2; packed.width=8; packed.height=8;
    packed.payload=Bytes(128,255);
    packed.payload[0]=packed.payload[1]=0;
    packed.payload[2]=0x20; packed.payload[3]=0;
    const auto api_packed=gpu_image_from_skin(packed,options);
    require(packed_pixel(api_packed,0)==0 && packed_pixel(api_packed,1)==0x8000,
            "API black key lost source565 green-low-bit distinction before target1555 reduction");
    options.runtime->automatic_transparency=true;
    const auto manual_packed=gpu_image_from_skin(packed,options);
    require(packed_pixel(manual_packed,0)==0 && packed_pixel(manual_packed,1)==0,
            "automatic packed key did not compare reduced post-upload1555 target");
    // Supplied RGB palette expands opaque alpha, matching the populated
    // original1002cf20/1002cf60 palette producer's peFlags=0xff.
    std::array<std::uint8_t,768> palette{};
    palette[0]=33; palette[1]=66; palette[2]=99; // GPU preparation forces only this entry black.
    palette[4]=1; // index1 nearblack is lifted to8,8,8 in the GPU-only preparation.
    palette[6]=200; palette[7]=100; palette[8]=20;
    palette[255*3]=40; palette[255*3+1]=80; palette[255*3+2]=120;
    Skin indexed; indexed.format=0; indexed.flags=0; indexed.width=8; indexed.height=8;
    indexed.payload=Bytes(64,2); indexed.payload[0]=0; indexed.payload[1]=1;
    indexed.payload[3]=255;
    const auto original_indices=indexed.payload;
    const auto original_palette=palette;
    const auto diagnostic=decode_skin_image(indexed,palette);
    const auto direct=gpu_image_from_skin(indexed,{},palette);
    require(!direct.runtime && direct.texels==diagnostic.rgba,
            "runtime palette preparation leaked into the default GPU source reader");
    options.overlay=false;
    const auto opaque=gpu_image_from_skin(indexed,options,palette);
    require(opaque.format==GpuImageFormat::Rgb565Unorm && !opaque.color_key_applied &&
            packed_pixel(opaque,0)==0 && packed_pixel(opaque,1)==0x0841 &&
            packed_pixel(opaque,2)==0xc322 && packed_pixel(opaque,3)==0x2a8f,
            "runtime palette black/nearblack preparation or custom255 normalized target mismatch");
    options.overlay=true;
    const auto dual=gpu_image_from_skin(indexed,options,palette);
    require(dual.format==GpuImageFormat::Argb1555Unorm && dual.color_key_applied &&
            packed_pixel(dual,0)==0 && packed_pixel(dual,1)==0x8421 &&
            packed_pixel(dual,2)==0xe182 && packed_pixel(dual,3)==0x954f,
            "prepared palette overlay lost API/manual key, nearest1555 or authored custom255");
    require(opaque.implementation_precision_fallback && dual.implementation_precision_fallback,
            "provided RGB palette fallback falsely claimed original packed conversion/FP precision");
    options.runtime->palette_is_global=true;
    const auto global=gpu_image_from_skin(indexed,options,palette);
    require(packed_pixel(global,3)==0xffff && packed_pixel(global,0)==0 &&
            packed_pixel(global,1)==0x8421,
            "verified global palette did not force255white while preserving preparation/key order");
    options.runtime->palette_is_global=false;
    require(diagnostic.rgba[0]==33 && diagnostic.rgba[1]==66 && diagnostic.rgba[2]==99 &&
            diagnostic.rgba[12]==40 && diagnostic.rgba[13]==80 && diagnostic.rgba[14]==120,
            "CPU palette diagnostic incorrectly received runtime black/global-white preparation");
    require(indexed.payload==original_indices && palette==original_palette &&
            decode_skin_image(indexed,palette).rgba==diagnostic.rgba,
            "runtime palette target conversion modified borrowed CPU indices/palette");
    options.runtime->automatic_transparency=false;
    options.runtime->bitmap_flags=0x10000;
    auto triangle=runtime_image(4,4);
    triangle.source_bits_per_pixel=24; triangle.source_alpha_flag=false;
    triangle.rgba[0]=triangle.rgba[1]=triangle.rgba[2]=0;
    const auto filtered_black=gpu_image_from_image(triangle,options);
    require(filtered_black.implementation_precision_fallback,
            "API-key TRIANGLE falsely claimed original FP byte identity");
    // First tent gives authored black weight9/16. API key clears that
    // source alpha BEFORE filtering, without normalizing surviving weights.
    for (std::size_t channel=0; channel<4; ++channel)
        require(filtered_black.texels[channel]==112 && filtered_black.texels[8+channel]==207,
                "API black substitution ran after TRIANGLE or renormalized keyed source weight");
}

void gpu_runtime_layout_behavior() {
    using namespace c4::assets;
    for (const auto caps : {std::array<std::uint32_t,3>{8192,16384,4096},
                           {2048,1024,1024},{4096,4096,4096},{128,192,256}}) {
        const auto cold=cold_gpu_image_context(caps[0],caps[1],true);
        require(cold.bitmap_flags==0 && cold.minimum_dimension==8 &&
                cold.maximum_dimension==caps[2] && cold.requested_mip_levels==0 &&
                cold.automatic_transparency && !cold.palette_is_global,
                "cold upload context did not use verified actual caps and recovered constructor seed");
    }
    rejects([]{ cold_gpu_image_context(0,1024,true); });
    rejects([]{ cold_gpu_image_context(1024,0,true); });
    rejects([]{ cold_gpu_image_context(1024,1024,false); });
    GpuImageUploadOptions mutable_options;
    mutable_options.runtime=GpuImageRuntimeContext{0,8,8192,0};
    const auto mutable_context=gpu_image_from_image(runtime_image(8192,1),mutable_options);
    require(mutable_context.width==8192 && mutable_context.runtime->nominal_width==8192,
            "cold4096 seed was incorrectly imposed as a permanent mutable runtime cap");
    GpuImageUploadOptions options;
    options.runtime=GpuImageRuntimeContext{0,8,256,0};
    auto particle=runtime_image(28,28);
    const auto gpu=gpu_image_from_image(particle,options);
    require(gpu.width==32 && gpu.height==32 && gpu.levels.size()==1 &&
            gpu.runtime->nominal_width==32 && gpu.runtime->nominal_height==32 &&
            gpu.runtime->written_width==28 && gpu.runtime->written_height==28 &&
            gpu.runtime->resize_filter==1 && !gpu.runtime->original_padding_known &&
            gpu.implementation_precision_fallback,
            "particle28 NONE extent/padding/mip request mismatch");
    for (std::size_t y=0; y<28; ++y)
        require(std::equal(particle.rgba.begin()+y*28*4,particle.rgba.begin()+(y+1)*28*4,
                           gpu.texels.begin()+y*32*4),"NONE stretched source rectangle");
    require(gpu.texels[28*4]==0,"defined native outside-write zero padding changed");
    options.runtime->bitmap_flags=0x40000;
    auto needle_source=runtime_image(156,25);
    needle_source.rgba[3]=0; needle_source.rgba[7]=17;
    options.overlay=true;
    const auto needle=gpu_image_from_image(needle_source,options);
    require(needle.width==256 && needle.height==32 && needle.levels.size()==4 &&
            needle.runtime->nominal_width==256 && needle.runtime->nominal_height==32 &&
            needle.runtime->written_width==156 && needle.runtime->written_height==25 &&
            needle.runtime->resize_filter==1 && !needle.runtime->original_padding_known &&
            needle.implementation_precision_fallback,
            "needle TARGET4 fabricated stretched pixels or known original margin");
    require(needle.source_alpha_flag && !needle.color_key_applied &&
            needle.texels[3]==0 && needle.texels[7]==17 &&
            needle_source.rgba[3]==0 && needle_source.rgba[7]==17,
            "private needle overlay replaced authored alpha100 with first-pixel key");
    options.overlay=false;
    options.runtime->bitmap_flags=0; options.runtime->maximum_dimension=128;
    const auto capped=gpu_image_from_image(runtime_image(156,25),options);
    require(capped.width==128 && capped.height==32 &&
            capped.runtime->nominal_width==256 && capped.runtime->nominal_height==32 &&
            capped.runtime->written_width==78 && capped.runtime->written_height==25 &&
            capped.runtime->resize_filter==4 && !capped.runtime->original_padding_known &&
            capped.implementation_precision_fallback,
            "max cap conflated nominal, physical and truncated write dimensions");
    options.runtime->bitmap_flags=0x10000; options.runtime->maximum_dimension=256;
    auto ramp=runtime_image(4,4);
    for (unsigned y=0; y<4; ++y) for (unsigned x=0; x<4; ++x)
        for (unsigned channel=0; channel<3; ++channel)
            ramp.rgba[(y*4+x)*4+channel]=static_cast<std::uint8_t>(16*x+64*y);
    const auto triangle=gpu_image_from_image(ramp,options);
    require(triangle.width==8 && triangle.height==8 &&
            triangle.runtime->written_width==8 && triangle.runtime->written_height==8 &&
            triangle.runtime->resize_filter==4 && triangle.runtime->original_padding_known &&
            triangle.implementation_precision_fallback && triangle.texels[0]==60 &&
            triangle.texels[4]==52,
            "TRIANGLE wrap integrated-tent native reference vector mismatch");
    ramp.source_bits_per_pixel=24; ramp.source_alpha_flag=false;
    options.overlay=true;
    const auto keyed=gpu_image_from_image(ramp,options);
    require(keyed.color_key_applied && keyed.texels[0]==60 && keyed.texels[1]==60 &&
            keyed.texels[2]==60 && keyed.texels[3]==0 &&
            keyed.texels[4]==52 && keyed.texels[7]==255,
            "private first-pixel key must capture post-resize target, preserve RGB and force nonmatches opaque");
    require(ramp.rgba[0]==0 && ramp.rgba[3]==255,
            "post-resize key mutated private CPU source");
    Skin low_green; low_green.format=2; low_green.flags=2;
    low_green.width=4; low_green.height=4;
    for (unsigned y=0; y<4; ++y) for (unsigned x=0; x<4; ++x)
        u16(low_green.payload,static_cast<std::uint16_t>((x==3 ? 2 : 1)<<5));
    const auto original_green=low_green.payload;
    const auto resized_packed=gpu_image_from_skin(low_green,options);
    require(resized_packed.format==GpuImageFormat::Argb1555Unorm &&
            resized_packed.implementation_precision_fallback && resized_packed.color_key_applied &&
            packed_pixel(resized_packed,0)==0x0020 && packed_pixel(resized_packed,1)==0x8000 &&
            low_green.payload==original_green,
            "TRIANGLE discarded RGB565 green low bit before target1555 filtering/keying");
    require(packed_pixel(resized_packed,7)==0x8020 &&
            packed_pixel(resized_packed,4*8)==0x8020,
            "manual key touched matching stretched texels outside authored width/height");
    auto constant=runtime_image(4,4);
    constant.source_bits_per_pixel=24; constant.source_alpha_flag=false;
    for (std::size_t at=0; at<constant.rgba.size(); at+=4)
        constant.rgba[at]=constant.rgba[at+1]=constant.rgba[at+2]=42;
    const auto bounded_key=gpu_image_from_image(constant,options);
    require(bounded_key.texels[3]==0 && bounded_key.texels[4*4+3]==255 &&
            bounded_key.texels[4*8*4+3]==255,
            "manual RGBA key used physical/write extent instead of authored rectangle");
    // Zero RGB565 padding is opaque black, not transparent RGBA padding.
    Skin packed; packed.format=2; packed.flags=2; packed.width=28; packed.height=28;
    packed.payload=Bytes(28*28*2,255);
    options.overlay=false; options.runtime->bitmap_flags=0;
    const auto packed_padding=gpu_image_from_skin(packed,options);
    const auto diagnostic=decode_gpu_image(packed_padding);
    require(diagnostic.rgba[28*4]==0 && diagnostic.rgba[28*4+3]==255 &&
            !packed_padding.runtime->original_padding_known,
            "native RGB565 zero padding was mislabeled transparent");
    Temp cube_temp; const auto cube_path=cube_temp.dir/"runtime-strip.tga";
    save(cube_path,cube_tga(12,2));
    const auto authored_cube=gpu_cubemap_from_image(load_gpu_image(cube_path));
    const auto padded_cube=gpu_cubemap_from_image(load_gpu_image(cube_path,options));
    require(padded_cube.width==2 && padded_cube.height==2 && padded_cube.face_count==6 &&
            padded_cube.texels==authored_cube.texels && padded_cube.runtime &&
            padded_cube.runtime->source_width==12 && padded_cube.runtime->source_height==2 &&
            padded_cube.runtime->nominal_width==16 && padded_cube.runtime->nominal_height==8 &&
            !padded_cube.runtime->original_padding_known,
            "runtime cube treated physical padded strip extent/pitch as logical six faces");
    options.runtime->maximum_dimension=0;
    rejects([&]{ gpu_image_from_image(particle,options); });
    options.runtime->maximum_dimension=16385;
    rejects([&]{ gpu_image_from_image(particle,options); });
    options.runtime->maximum_dimension=16384; options.runtime->minimum_dimension=16384;
    rejects([&]{ gpu_image_from_image(runtime_image(1,1),options); });
    options.runtime->minimum_dimension=0;
    rejects([&]{ gpu_image_from_image(particle,options); });
    options.runtime->minimum_dimension=8; options.runtime->maximum_dimension=256;
    options.runtime->requested_mip_levels=7;
    rejects([&]{ gpu_image_from_image(runtime_image(8,8),options); });
    options.runtime->requested_mip_levels=1;
    const auto unsupported=[&](const char* message, const auto& operation) {
        try { operation(); }
        catch (const std::runtime_error& error) {
            require(std::string(error.what()).find(message)!=std::string::npos,
                    "unsupported runtime derivation lacks honest explanation");
            return;
        }
        require(false,"unsupported runtime derivation was silently accepted");
    };
    auto unsafe_key=runtime_image(8,8);
    unsafe_key.source_bits_per_pixel=24; unsafe_key.source_alpha_flag=false;
    options.overlay=true; options.runtime->maximum_dimension=4;
    unsupported("manual-key authored rectangle exceeds physical mip surface",
                [&]{ gpu_image_from_image(unsafe_key,options); });
    options.overlay=false;
    options.runtime=GpuImageRuntimeContext{0x10000,1,6,3};
    auto checker=runtime_image(6,6);
    for (unsigned y=0; y<6; ++y) for (unsigned x=0; x<6; ++x)
        for (unsigned channel=0; channel<4; ++channel)
            checker.rgba[(y*6+x)*4+channel]=(x&1) ? 255 : 0;
    const auto dithered=gpu_image_from_image(checker,options);
    require(dithered.width==6 && dithered.height==6 && dithered.levels.size()==3 &&
            dithered.runtime->nominal_width==8 && dithered.runtime->nominal_height==8 &&
            dithered.runtime->written_width==6 && dithered.runtime->written_height==6 &&
            dithered.runtime->mip_filter==0x80004 && dithered.implementation_precision_fallback,
            "NPOT DEFAULT did not select original TRIANGLE|DITHER policy");
    constexpr std::array<std::uint8_t,9> half_codes{128,127,128,127,128,127,128,127,128};
    for (std::size_t pixel=0; pixel<9; ++pixel) for (std::size_t channel=0; channel<4; ++channel)
        require(dithered.texels[dithered.levels[1].offset+pixel*4+channel]==half_codes[pixel],
                "NPOT ordered dither lost original odd-row serpentine phase or alpha");
    for (std::size_t channel=0; channel<4; ++channel)
        require(dithered.texels[dithered.levels[2].offset+channel]==128,
                "NPOT fixed initial source half-code callback mismatch");
    auto impulse=runtime_image(6,6);
    std::fill(impulse.rgba.begin(),impulse.rgba.end(),127);
    std::fill_n(impulse.rgba.begin(),4,128);
    const auto reused=gpu_image_from_image(impulse,options);
    // First tent weight49/256 crosses the31/32 quantizer bias; wrapped
    // corner weights7/256,7/256,1/256 do not. Fixed6x6 mean1/36 stays
    // below the final boundary, unlike recursively quantized3x3 mean1/9.
    for (std::size_t pixel=0; pixel<9; ++pixel) for (std::size_t channel=0; channel<4; ++channel)
        require(reused.texels[reused.levels[1].offset+pixel*4+channel]==(pixel==0 ? 128 : 127),
                "NPOT impulse ordered quantization mismatch");
    for (std::size_t channel=0; channel<4; ++channel)
        require(reused.texels[reused.levels[2].offset+channel]==127,
                "NPOT generated suffix recursively reused quantized output instead of fixed SrcLevel");
}

void gpu_runtime_encoded_behavior() {
    using namespace c4::assets;
    Temp temp; const auto path=temp.dir/"masked-runtime.dds";
    Bytes bytes(128);
    put32(bytes,0,0x20534444); put32(bytes,4,124); put32(bytes,8,0x2100f);
    put32(bytes,12,8); put32(bytes,16,8); put32(bytes,20,32); put32(bytes,28,4);
    put32(bytes,76,32); put32(bytes,80,0x41); put32(bytes,88,32);
    put32(bytes,92,0xff0000); put32(bytes,96,0xff00);
    put32(bytes,100,0xff); put32(bytes,104,0xff000000); put32(bytes,108,0x401008);
    for (unsigned y=0; y<8; ++y) for (unsigned x=0; x<8; ++x)
        bytes.insert(bytes.end(),{0,0,static_cast<std::uint8_t>(x+8*y),255});
    for (unsigned pixel=0; pixel<21; ++pixel) bytes.insert(bytes.end(),{200,100,50,17});
    save(path,bytes);
    const auto cpu=load_image(path);
    const auto direct=load_gpu_image(path);
    require(!direct.runtime && direct.levels.size()==4 &&
            direct.texels[direct.levels[1].offset]==50,
            "default masked DDS lost authored lower levels");
    GpuImageUploadOptions options{true};
    options.runtime=GpuImageRuntimeContext{0,8,256,0};
    Image retained_cpu;
    const auto gpu=load_gpu_image(path,retained_cpu,options);
    require(gpu.runtime && gpu.runtime->encoded_postfilter_attempted &&
            gpu.runtime->postfilter_output_known && gpu.runtime->authored_mip_levels==4 &&
            gpu.runtime->preserved_mip_levels==1 && gpu.runtime->generated_mip_levels==3 &&
            gpu.authored_dds==bytes && read_resource(path)==bytes &&
            retained_cpu.rgba==cpu.rgba && retained_cpu.mip_rgba==cpu.mip_rgba &&
            retained_cpu.width==8 && retained_cpu.height==8 && !gpu.color_key_applied,
            "encoded runtime lost original bytes/CPU diagnostic or skipped original postfilter");
    constexpr std::array<std::uint8_t,3> first_red{5,14,32};
    for (std::size_t lod=1; lod<4; ++lod) {
        const auto at=gpu.levels[lod].offset;
        require(gpu.texels[at]==first_red[lod-1] && gpu.texels[at+1]==0 &&
                gpu.texels[at+2]==0 && gpu.texels[at+3]==255,
                "encoded mip postfilter silently preserved authored lower sentinel");
    }
}

void gpu_runtime_bc_behavior() {
    using namespace c4::assets;
    Temp temp; const auto path=temp.dir/"runtime-bc.dds";
    constexpr std::array formats{GpuImageFormat::Bc1Unorm,GpuImageFormat::Bc2Unorm,
                                GpuImageFormat::Bc3Unorm};
    constexpr std::array fourcc{0x31545844u,0x33545844u,0x35545844u};
    GpuImageUploadOptions options{true};
    options.runtime=GpuImageRuntimeContext{0,8,256,0};
    for (std::size_t format=0; format<formats.size(); ++format) {
        const auto block_bytes=detail::bc_block_bytes(formats[format]);
        // Independent constant vectors from the original wrapper/degenerate
        // RGB solver, not encoder-produced expected data.
        Bytes white;
        if (format==1) white=Bytes(8,255);
        else if (format==2) white={255,255,0,0,0,0,0,0};
        white.insert(white.end(),{255,255,255,255,0,0,0,0});
        detail::BcBlock pixels;
        pixels.fill({1.f,1.f,1.f,1.f});
        Bytes encoded(block_bytes);
        detail::encode_bc_block(formats[format],pixels,encoded);
        require(encoded==white,"original BC constant-white block vector mismatch");
        detail::encode_bc_block(formats[format],pixels,encoded,true);
        require(encoded==white,"original BC diffusion changed exact constant-white block");
        detail::decode_bc_block(formats[format],white,pixels);
        for (const auto& pixel : pixels)
            require(pixel==detail::BcPixel{1.f,1.f,1.f,1.f},
                    "original BC normalized constant-white decode mismatch");
        if (format==0) {
            pixels.fill({1.f,1.f,1.f,0.f});
            detail::encode_bc_block(formats[format],pixels,encoded);
            require(encoded==Bytes({0,0,255,255,255,255,255,255}),
                    "original BC1 all-transparent sentinel mismatch");
        } else if (format==1) {
            pixels.fill({1.f,1.f,1.f,0.f});
            detail::encode_bc_block(formats[format],pixels,encoded);
            require(std::all_of(encoded.begin(),encoded.begin()+8,[](auto byte){ return byte==0; }),
                    "original BC2 zero-alpha nibble quantization mismatch");
            for (std::size_t pixel=0; pixel<16; ++pixel) pixels[pixel][3]=(pixel&1) ? 1.f : 0.f;
            detail::encode_bc_block(formats[format],pixels,encoded);
            require(std::all_of(encoded.begin(),encoded.begin()+8,[](auto byte){ return byte==0xf0; }),
                    "original BC2 alternating alpha nibble order mismatch");
        }
        auto bytes=gpu_dds_fixture(fourcc[format],block_bytes);
        for (std::size_t block=0; block<7; ++block)
            std::copy(white.begin(),white.end(),bytes.begin()+128+block*block_bytes);
        save(path,bytes);
        auto source=load_gpu_image(path);
        const auto* original_buffer=source.texels.data();
        require(source.texels==bytes && source.authored_dds.empty() && !source.runtime,
                "default BC source reader acquired derivative bytes or second DDS copy");
        const auto derived=derive_gpu_image_upload(std::move(source),*options.runtime,true);
        require(derived.authored_dds.data()==original_buffer && derived.authored_dds==bytes,
                "compressed runtime copied/lost original immutable authored DDS buffer");
        require(derived.runtime && derived.runtime->encoded_postfilter_attempted &&
                derived.runtime->postfilter_output_known && derived.runtime->authored_mip_levels==4 &&
                derived.runtime->preserved_mip_levels==1 && derived.runtime->generated_mip_levels==3 &&
                derived.runtime->mip_filter==5 && derived.implementation_precision_fallback &&
                !derived.color_key_applied && derived.format==formats[format],
                "BC runtime misrepresented attempted postfilter, precision, format or keying");
        require_gpu_level(derived.levels[0],8,8,0,4*block_bytes,2*block_bytes,4*block_bytes);
        require_gpu_level(derived.levels[1],4,4,4*block_bytes,block_bytes,block_bytes,block_bytes);
        require_gpu_level(derived.levels[2],2,2,5*block_bytes,block_bytes,block_bytes,block_bytes);
        require_gpu_level(derived.levels[3],1,1,6*block_bytes,block_bytes,block_bytes,block_bytes);
        require(derived.texels.size()==7*block_bytes,"BC runtime upload contains gaps/header bytes");
        for (std::size_t block=0; block<7; ++block)
            require(std::equal(white.begin(),white.end(),derived.texels.begin()+block*block_bytes),
                    "BC constant-white recursive BOX or partial block contents changed");
        const auto cpu=load_image(path);
        Image retained_cpu;
        const auto paired=load_gpu_image(path,retained_cpu,options);
        require(paired.texels==derived.texels && paired.authored_dds==bytes &&
                retained_cpu.rgba==cpu.rgba && retained_cpu.mip_rgba==cpu.mip_rgba &&
                retained_cpu.width==8 && retained_cpu.height==8 &&
                retained_cpu.mip_levels.size()==3 && read_resource(path)==bytes,
                "BC runtime modified CPU diagnostics or original source resource");
        // Logical pixels are regenerated; existing authored outside-logical
        // pixels seed partial blocks. Whole-block refitting is lossy: assert
        // their influence, not exact preservation of outside decoded colors.
        auto seeded=bytes;
        for (const auto level : {1u,2u,3u}) {
            const auto block_at=128+(level==1 ? 4 : level==2 ? 5 : 6)*block_bytes;
            const auto color_at=block_at+(format==0 ? 0 : 8);
            const std::uint16_t color=level==1 ? 0 : level==2 ? 0x07e0 : 0x001f;
            for (std::size_t endpoint=0; endpoint<2; ++endpoint) {
                seeded[color_at+endpoint*2]=static_cast<std::uint8_t>(color);
                seeded[color_at+endpoint*2+1]=static_cast<std::uint8_t>(color>>8);
            }
        }
        save(path,seeded);
        const auto seeded_cpu=load_image(path);
        const auto partial=load_gpu_image(path,retained_cpu,options);
        require(partial.authored_dds==seeded && read_resource(path)==seeded &&
                retained_cpu.rgba==seeded_cpu.rgba && retained_cpu.mip_rgba==seeded_cpu.mip_rgba,
                "partial BC filtering overwrote immutable authored source");
        require(std::equal(white.begin(),white.end(),partial.texels.begin()+partial.levels[1].offset),
                "BC postfilter did not replace authored black LOD1 from white base");
        for (std::size_t lod=2; lod<4; ++lod) {
            const auto& level=partial.levels[lod];
            require(!std::equal(white.begin(),white.end(),partial.texels.begin()+level.offset),
                    "partial BC writer zeroed/ignored existing authored outside-logical pixels");
            const auto authored_at=128+(lod==2 ? 5 : 6)*block_bytes;
            require(!std::equal(seeded.begin()+authored_at,seeded.begin()+authored_at+block_bytes,
                                partial.texels.begin()+level.offset),
                    "BC postfilter silently retained authored green/blue small mip");
        }
        auto capped_source=bytes;
        put32(capped_source,28,3); capped_source.resize(128+6*block_bytes);
        save(path,capped_source);
        options.runtime->maximum_dimension=6;
        const auto npot=load_gpu_image(path,options);
        require(npot.width==6 && npot.height==6 && npot.levels.size()==3 &&
                npot.runtime->nominal_width==8 && npot.runtime->nominal_height==8 &&
                npot.runtime->mip_filter==0x80004 && npot.implementation_precision_fallback &&
                npot.authored_dds==capped_source && !npot.runtime->original_padding_known,
                "BC cap/NPOT runtime omitted derived layout, dither or fresh partial-block uncertainty");
        require_gpu_level(npot.levels[0],6,6,0,4*block_bytes,2*block_bytes,4*block_bytes);
        require_gpu_level(npot.levels[1],3,3,4*block_bytes,block_bytes,block_bytes,block_bytes);
        require_gpu_level(npot.levels[2],1,1,5*block_bytes,block_bytes,block_bytes,block_bytes);
        require(std::equal(white.begin(),white.end(),npot.texels.begin()),
                "BC capped fully written constant block lost independent white vector");
        options.runtime->maximum_dimension=256;
    }
}

void gpu_runtime_bc_source_reuse_behavior() {
    using namespace c4::assets;
    Temp temp; const auto path=temp.dir/"bc2-fixed-source.dds";
    GpuImageUploadOptions options;
    options.runtime=GpuImageRuntimeContext{0,8,6,0};
    // For a1x1 TRIANGLE destination, integrated wrapped tents assign equal
    // area to every logical source texel. This mean oracle does not invoke
    // the runtime filter or reparse DDS; the block codec has separate vectors.
    const auto terminal_block=[](const GpuImage& image, std::size_t lod) {
        const auto& level=image.levels.at(lod);
        std::array<double,4> sum{};
        for (std::uint32_t by=0; by<(level.height+3)/4; ++by)
            for (std::uint32_t bx=0; bx<(level.width+3)/4; ++bx) {
                detail::BcBlock block;
                detail::decode_bc_block(image.format,
                    std::span<const std::uint8_t>(image.texels).subspan(
                        level.offset+by*level.row_pitch+bx*16,16),block);
                for (unsigned y=0; y<4 && by*4+y<level.height; ++y)
                    for (unsigned x=0; x<4 && bx*4+x<level.width; ++x)
                        for (std::size_t channel=0; channel<4; ++channel)
                            sum[channel]+=block[y*4+x][channel];
            }
        // Cap8→6 creates6/3/1 surfaces, none matching authored8/4/2:
        // the defined fresh destination seed is zero, marked unknown padding.
        std::array<std::uint8_t,16> bytes{};
        detail::BcBlock destination;
        detail::decode_bc_block(image.format,bytes,destination);
        const auto pixels=double(level.width)*level.height;
        for (std::size_t channel=0; channel<4; ++channel)
            destination[0][channel]=static_cast<float>(sum[channel]/pixels);
        detail::encode_bc_block(image.format,destination,bytes,true);
        return bytes;
    };
    bool discriminating=false;
    // Deterministic bounded sparse-alpha corpus, not an invented pre-cap
    // oracle: compare each REAL6x6 upload to an independent full-area mean.
    for (const unsigned high : {9u,12u,15u}) for (const unsigned low : {3u,6u,9u})
        for (const unsigned first_x : {0u,1u}) for (const unsigned row : {0u,1u}) {
            auto bytes=gpu_dds_fixture(0x33545844u,16);
            put32(bytes,28,3); bytes.resize(128+6*16);
            for (std::size_t block=0; block<6; ++block) {
                const auto at=128+block*16;
                std::fill_n(bytes.begin()+at,16,0);
                std::fill_n(bytes.begin()+at+8,4,255); // opaque-white RGB endpoints
            }
            const auto alpha=[&](unsigned x, unsigned y, unsigned code) {
                const auto pixel=(y%4)*4+x%4;
                const auto at=128+((y/4)*2+x/4)*16+pixel/2;
                bytes[at]|=static_cast<std::uint8_t>(code<<((pixel%2)*4));
            };
            alpha(first_x,row,high); alpha(3,row,high); alpha(6,row,low);
            save(path,bytes);
            auto authored=load_gpu_image(path);
            const auto* buffer=authored.texels.data();
            const auto image=derive_gpu_image_upload(std::move(authored),*options.runtime,false);
            require(image.width==6 && image.height==6 && image.levels.size()==3 &&
                    image.runtime->mip_filter==0x80004 && image.implementation_precision_fallback &&
                    !image.runtime->original_padding_known && image.authored_dds==bytes &&
                    image.authored_dds.data()==buffer && read_resource(path)==bytes,
                    "BC source-reuse corpus lost explicit context, precision or immutable authored buffer");
            const auto fixed=terminal_block(image,0);
            const auto recursive=terminal_block(image,1);
            discriminating=discriminating || fixed!=recursive;
            const auto at=image.levels[2].offset;
            require(std::equal(fixed.begin(),fixed.end(),image.texels.begin()+at),
                    "BC NPOT second generated LOD reused quantized previous output instead of initial SrcLevel0");
        }
    require(discriminating,"BC source-reuse corpus failed to distinguish fixed and recursive generation");
}
}
int main(int argc, char** argv) {
    try {
        skin_behavior(); upper_left_color_key(); behavior(); cubemap_payload(); cubemap_orientation(); cubemap_errors();
        source_tangent_behavior();
        gpu_dds_behavior(); gpu_dds_alpha_behavior(); gpu_bmp_behavior();
        gpu_tga_behavior(); gpu_cube_behavior(); gpu_skin_behavior();
        gpu_sky_faces_behavior();
        gpu_runtime_box_behavior(); gpu_runtime_layout_behavior();
        gpu_runtime_key_modes_behavior();
        gpu_runtime_encoded_behavior();
        gpu_runtime_bc_behavior();
        gpu_runtime_bc_source_reuse_behavior();
        if (argc>1) original_cubemaps(argv[1]);
        std::cout << "asset behavior checks passed\n"; return 0;
    }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
