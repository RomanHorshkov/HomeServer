file(REMOVE_RECURSE
  "../../../../lib/.1"
  "../../../../lib/libemlog.pdb"
  "../../../../lib/libemlog.so"
  "../../../../lib/libemlog.so.1"
  "../../../../lib/libemlog.so.1.0.0"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/emlog_shared.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
