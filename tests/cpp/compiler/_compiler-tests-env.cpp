#include <gtest/gtest.h>

#include "compiler/compiler-core.h"

class CompilerTestsEnvironment : public testing::Environment {
public:
  ~CompilerTestsEnvironment() final {}

  void SetUp() final {
    testing::Environment::SetUp();
    G = new CompilerCore();
    G->register_env(new KphpEnviroment{});
  }

  void TearDown() final {
    testing::Environment::TearDown();
  }
};

const testing::Environment* compiler_tests_env = testing::AddGlobalTestEnvironment(new CompilerTestsEnvironment);