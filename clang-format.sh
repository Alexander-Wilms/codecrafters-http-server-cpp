# https://stackoverflow.com/questions/28896909/how-to-call-clang-format-over-a-cpp-project-folder#comment83912792_36046965
# https://stackoverflow.com/a/54424991/2278742
find . \( -iname "*.cpp" -o -iname "*.h" \) -print0 | xargs -0 clang-format -i
