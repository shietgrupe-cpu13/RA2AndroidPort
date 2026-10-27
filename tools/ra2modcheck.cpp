#include "ra2assets/mod_profile.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: ra2modcheck <mod-folder>\n";return 2;}
    auto mod=ra2assets::ModProfile::inspect(argv[1]);std::cout<<mod->report();
    return mod->can_activate()?0:1;
}
