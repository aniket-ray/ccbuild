## Summary

<!-- Brief description of the changes and why they are needed -->

## Type of Change

- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Refactoring
- [ ] Documentation
- [ ] CI / Build

## Testing

<!-- Describe how you tested your changes -->

- [ ] All tests pass: `ctest --test-dir build --output-on-failure -j`
- [ ] New gtest tests added for new functionality
- [ ] Clang-format check passes: `find src include examples tests -name '*.cc' -o -name '*.h' | xargs clang-format --Werror --dry-run && clang-format --Werror --dry-run build.cc`
- [ ] No warnings with `-DCCBUILD_WARNINGS_AS_ERRORS=ON`
- [ ] New source files added to both `CMakeLists.txt` and `build.cc`
- [ ] Coverage report looks good: `cmake --build build --target coverage`

## Checklist

- [ ] I have written/updated gtest tests for my changes
- [ ] All tests pass locally with `ctest`
- [ ] Clang-format compliance on changed files
- [ ] My changes generate no new compiler warnings
- [ ] I have updated documentation if needed
