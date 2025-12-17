#define CATCH_CONFIG_MAIN
#include "../../third_party/catch2/catch_amalgamated.hpp"
#include <string>

extern int8_t submitPropGetRequest(const std::string& prop,
                                   std::string& buffer,
                                   const std::string& defaultValue);

TEST_CASE("submitPropGetRequest returns default when property is missing", "[props]") {
    const std::string propName = "unit.test.nonexistent.property";
    std::string out;
    const std::string defaultValue = "na";

    int8_t found = submitPropGetRequest(propName, out, defaultValue);

    REQUIRE(found == false);
    REQUIRE(out == defaultValue);
}
