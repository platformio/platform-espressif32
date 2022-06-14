#include <unity.h>

void test_dummy(void)
{
  TEST_ASSERT_EQUAL(1, 1);
}

void app_main()
{
  UNITY_BEGIN();

  RUN_TEST(test_dummy);

  UNITY_END();
}