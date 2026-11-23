#include <platform/bo1_texture_match.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"linker test failed at %d\n",__LINE__);std::exit(1);}}while(0)
int main(){
 using bo1::texture_match::ExactImage;
 using bo1::texture_match::ZombieModel;
 CHECK(ExactImage("zombie_skin","images/zombie_skin.iwi"));
 CHECK(ExactImage("images\\ZOMBIE_Skin.iwi","images/zombie_skin.iwi"));
 CHECK(ExactImage("~zombie_face","images/~zombie_face.iwi"));
 CHECK(!ExactImage("zombie_skin","images/grass_piece1.iwi"));
 CHECK(!ExactImage("zombie_skin","images/zombie_skin_alt.iwi"));
 CHECK(!ExactImage("grass_piece1","images/grass_piece11.iwi"));
 CHECK(!ExactImage("","images/grass_piece1.iwi"));
 CHECK(ZombieModel("char_zombie_pentagon"));
 CHECK(ZombieModel("ZOM_Walker_05"));
 CHECK(!ZombieModel("foliage_grass"));
 std::puts("Exact material/image matching tests passed");
}
