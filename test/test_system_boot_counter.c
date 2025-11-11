#include "unity.h"

#include "system_boot_counter.h"

TEST_CASE("boot_counter_initializes_to_zero", "[boot_counter]")
{
    system_boot_counter_mock_reset();
    TEST_ASSERT_EQUAL_UINT32(0U, system_boot_counter_get());
}

TEST_CASE("boot_counter_increments_and_persists", "[boot_counter]")
{
    system_boot_counter_mock_reset();

    uint32_t value = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, system_boot_counter_increment_and_get(&value));
    TEST_ASSERT_EQUAL_UINT32(1U, value);
    TEST_ASSERT_EQUAL_UINT32(1U, system_boot_counter_get());

    TEST_ASSERT_EQUAL(ESP_OK, system_boot_counter_increment_and_get(&value));
    TEST_ASSERT_EQUAL_UINT32(2U, value);
    TEST_ASSERT_EQUAL_UINT32(2U, system_boot_counter_get());
}

TEST_CASE("boot_counter_mock_set_overrides_value", "[boot_counter]")
{
    system_boot_counter_mock_reset();
    system_boot_counter_mock_set(41U);

    TEST_ASSERT_EQUAL_UINT32(41U, system_boot_counter_get());

    uint32_t value = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, system_boot_counter_increment_and_get(&value));
    TEST_ASSERT_EQUAL_UINT32(42U, value);
    TEST_ASSERT_EQUAL_UINT32(42U, system_boot_counter_get());
}

