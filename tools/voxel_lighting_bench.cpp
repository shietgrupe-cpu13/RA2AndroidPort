// Synthetic cold-raster benchmark. No retail assets or foreign engine code.
#include "voxel_raster.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <vector>
int main(int argc,char**) {
 using namespace ra2engine;
 ra2assets::VxlModel m;ra2assets::VxlSection s;s.name="body";s.size_x=40;s.size_y=40;s.size_z=24;
 s.scale=0.0625;s.min_bounds[0]=s.min_bounds[1]=-20;s.min_bounds[2]=0;
 s.max_bounds[0]=s.max_bounds[1]=20;s.max_bounds[2]=24;s.normals_mode=4;
 s.voxels.resize(40*40*24);
 for(std::size_t i=0;i<s.voxels.size();++i){auto& v=s.voxels[i];v.present=(i%7)!=0;v.normal=static_cast<unsigned char>(i%256);v.color=static_cast<unsigned char>(1+i%255);}
 m.sections.push_back(s);
 ra2assets::HvaModel h;h.frame_count=2;h.section_names={"body"};h.frames.resize(2,std::vector<ra2assets::HvaSectionXform>(1));
 for(auto& f:h.frames){auto& t=f[0].transform;t[0][0]=t[1][1]=t[2][2]=1;}
 h.frames[1][0].transform[0][1]=0.125;h.frames[1][0].transform[1][0]=-0.25;h.frames[1][0].transform[2][3]=5;
 ra2assets::Vpl vpl;vpl.section_count=32;vpl.tables.resize(32);
 for(int l=0;l<32;++l)for(int c=0;c<256;++c)vpl.tables[l][c]=static_cast<unsigned char>((c+l*7)%256);
 ra2assets::Palette pal{};for(int c=0;c<256;++c)pal[c]={static_cast<unsigned char>(c),static_cast<unsigned char>(255-c),static_cast<unsigned char>((c*17)%256),255};
 auto hash=[](const VoxelSprite& image){std::uint64_t n=14695981039346656037ull;for(auto b:image.rgba)n=(n^b)*1099511628211ull;return n;};
 if(argc>1){
  for(int mode:{2,4})for(int frame:{0,1})for(int ramp:{0,1,5,13,1024+(1*17+5)*9+4}) {
   m.sections[0].normals_mode=mode;
   auto image=rasterize_model(m,h,index_to_yaw(3,32),vpl,pal,frame,0.12,ramp);
   std::cout<<mode<<' '<<frame<<' '<<ramp<<' '<<image.w<<' '<<image.h<<' '<<image.anchor_x<<' '<<image.anchor_y<<' '<<hash(image)<<'\n';
  }
  return 0;
 }
 std::vector<double> times;std::uint64_t sum=0;
 for(int sample=0;sample<9;++sample){auto start=std::chrono::steady_clock::now();
  for(int i=0;i<180;++i){auto image=rasterize_model(m,h,index_to_yaw(i%32,32),vpl,pal,i%2,0.12,i%3?0:5);sum+=static_cast<std::uint64_t>(image.w*1000+image.h)+image.rgba[i%image.rgba.size()];}
  times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
 }
 std::sort(times.begin(),times.end());std::cout<<std::fixed<<std::setprecision(3)<<"median_ms="<<times[4]<<" min_ms="<<times.front()<<" max_ms="<<times.back()<<" guard="<<sum<<'\n';
}
