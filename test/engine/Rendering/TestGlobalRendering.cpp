/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <catch_amalgamated.hpp>

#include <map>
#include <string>
#include <vector>

#include "Rendering/GlobalRendering.h"
#include "System/Config/ConfigHandler.h"

namespace {

class StubConfigHandler : public ConfigHandler {
public:
	std::map<std::string, std::string> values;

	void FinalizeLoad() override {}
	void SetString(const std::string& key, const std::string& value, bool, bool) override
	{
		values[key] = value;
	}
	std::string GetString(const std::string& key) const override
	{
		auto it = values.find(key);
		return (it != values.end()) ? it->second : std::string{};
	}
	bool IsSet(const std::string& key) const override
	{
		return values.find(key) != values.end();
	}
	bool IsReadOnly(const std::string&) const override { return false; }
	bool IsDeprecated(const std::string&) const override { return false; }
	void Delete(const std::string& key) override { values.erase(key); }
	std::string GetConfigFile() const override { return {}; }
	const std::map<std::string, std::string> GetData() const override { return values; }
	std::map<std::string, std::string> GetDataWithoutDefaults() const override { return values; }
	void Update() override {}
	void EnableWriting(bool) override {}

protected:
	void AddObserver(ConfigNotifyCallback, void*, const std::vector<std::string>&) override {}
	void RemoveObserver(void*) override {}
};

ConfigHandler* configHandler = nullptr;

} // namespace

bool ConfigHandler::Get(const std::string& key) const
{
	const std::string val = GetString(key);
	return (val == "1" || val == "true");
}

namespace {

struct GlobalRenderingFixture {
	StubConfigHandler stub;

	GlobalRenderingFixture() { configHandler = &stub; }
	~GlobalRenderingFixture() { configHandler = nullptr; }
};

// Mirror of CGlobalRendering::SetFullScreen state transitions
struct WindowedModeHarness {
	bool fullScreen = false;

	void SetFullScreen(bool cliWindowed, bool cliFullScreen)
	{
		const bool cfgFullScreen = configHandler->GetBool("Fullscreen");
		fullScreen = CGlobalRendering::ResolveFullScreenMode(cfgFullScreen, cliWindowed, cliFullScreen);
		configHandler->Set("Fullscreen", fullScreen);
	}
};

} // namespace

TEST_CASE("GlobalRendering: ResolveFullScreenMode truth table", "[GlobalRendering]")
{
	SECTION("cliWindowed has absolute precedence over cliFullScreen and config") {
		// Even when both config and cliFullScreen request fullscreen:
		CHECK_FALSE(CGlobalRendering::ResolveFullScreenMode(true,  /*cliWindowed=*/true, /*cliFullScreen=*/true));
		CHECK_FALSE(CGlobalRendering::ResolveFullScreenMode(false, /*cliWindowed=*/true, /*cliFullScreen=*/true));

		// When only config requests fullscreen:
		CHECK_FALSE(CGlobalRendering::ResolveFullScreenMode(true,  /*cliWindowed=*/true, /*cliFullScreen=*/false));
		CHECK_FALSE(CGlobalRendering::ResolveFullScreenMode(false, /*cliWindowed=*/true, /*cliFullScreen=*/false));
	}

	SECTION("cliFullScreen enables fullscreen when cliWindowed is false") {
		CHECK(CGlobalRendering::ResolveFullScreenMode(false, /*cliWindowed=*/false, /*cliFullScreen=*/true));
		CHECK(CGlobalRendering::ResolveFullScreenMode(true,  /*cliWindowed=*/false, /*cliFullScreen=*/true));
	}

	SECTION("Falls back to cfgFullScreen when neither CLI flag is specified") {
		CHECK(CGlobalRendering::ResolveFullScreenMode(true,  /*cliWindowed=*/false, /*cliFullScreen=*/false));
		CHECK_FALSE(CGlobalRendering::ResolveFullScreenMode(false, /*cliWindowed=*/false, /*cliFullScreen=*/false));
	}
}

TEST_CASE_METHOD(GlobalRenderingFixture, "GlobalRendering: SetFullScreen config synchronization", "[GlobalRendering]")
{
	WindowedModeHarness harness;

	SECTION("cliWindowed overrides config Fullscreen=1 and updates config to 0") {
		stub.values["Fullscreen"] = "1";
		harness.SetFullScreen(/*cliWindowed=*/true, /*cliFullScreen=*/false);

		CHECK_FALSE(harness.fullScreen);
		CHECK(stub.values["Fullscreen"] == "0");
		CHECK_FALSE(configHandler->GetBool("Fullscreen"));
	}

	SECTION("cliWindowed overrides conflicting cliFullScreen=true and Fullscreen=1") {
		stub.values["Fullscreen"] = "1";
		harness.SetFullScreen(/*cliWindowed=*/true, /*cliFullScreen=*/true);

		CHECK_FALSE(harness.fullScreen);
		CHECK(stub.values["Fullscreen"] == "0");
		CHECK_FALSE(configHandler->GetBool("Fullscreen"));
	}

	SECTION("cliFullScreen overrides config Fullscreen=0 and updates config to 1") {
		stub.values["Fullscreen"] = "0";
		harness.SetFullScreen(/*cliWindowed=*/false, /*cliFullScreen=*/true);

		CHECK(harness.fullScreen);
		CHECK(stub.values["Fullscreen"] == "1");
		CHECK(configHandler->GetBool("Fullscreen"));
	}

	SECTION("No CLI flags respects config Fullscreen=1") {
		stub.values["Fullscreen"] = "1";
		harness.SetFullScreen(/*cliWindowed=*/false, /*cliFullScreen=*/false);

		CHECK(harness.fullScreen);
		CHECK(stub.values["Fullscreen"] == "1");
		CHECK(configHandler->GetBool("Fullscreen"));
	}

	SECTION("No CLI flags respects config Fullscreen=0") {
		stub.values["Fullscreen"] = "0";
		harness.SetFullScreen(/*cliWindowed=*/false, /*cliFullScreen=*/false);

		CHECK_FALSE(harness.fullScreen);
		CHECK(stub.values["Fullscreen"] == "0");
		CHECK_FALSE(configHandler->GetBool("Fullscreen"));
	}
}
