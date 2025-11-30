file(REMOVE_RECURSE
  "../../../../lib/libspsc_ring.a"
  "../../../../lib/libspsc_ring.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/spsc_ring_static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
