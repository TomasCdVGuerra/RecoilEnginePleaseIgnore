/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <catch_amalgamated.hpp>

#include <map>
#include <string>
#include <vector>

#include "System/Input/MouseInput.h"
#include "System/Input/InputHandler.h"
#include "Rendering/GlobalRendering.h"
#include "Game/UI/MouseHandler.h"
#include "System/Config/ConfigHandler.h"

// Stubs for engine globals required by MouseInput
CMouseHandler* mouse = nullptr;
CGlobalRendering* globalRendering = nullptr;

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

CGlobalRendering::CGlobalRendering() {}
CGlobalRendering::~CGlobalRendering() {}

struct MockMouseHandlerState {
	int lastMoveX = -1;
	int lastMoveY = -1;
	int lastMoveDx = 0;
	int lastMoveDy = 0;
	int lastPressX = -1;
	int lastPressY = -1;
	int lastPressBtn = 0;
	int lastReleaseX = -1;
	int lastReleaseY = -1;
	int lastReleaseBtn = 0;
	int windowEnterCount = 0;
	int windowLeaveCount = 0;
};
static MockMouseHandlerState mockMouseState;

void CMouseHandler::MouseMove(int x, int y, int dx, int dy)
{
	mockMouseState.lastMoveX = x;
	mockMouseState.lastMoveY = y;
	mockMouseState.lastMoveDx = dx;
	mockMouseState.lastMoveDy = dy;
}
void CMouseHandler::MousePress(int x, int y, int button)
{
	mockMouseState.lastPressX = x;
	mockMouseState.lastPressY = y;
	mockMouseState.lastPressBtn = button;
}
void CMouseHandler::MouseRelease(int x, int y, int button)
{
	mockMouseState.lastReleaseX = x;
	mockMouseState.lastReleaseY = y;
	mockMouseState.lastReleaseBtn = button;
}
void CMouseHandler::MouseWheel(float delta) {}
void CMouseHandler::WindowEnter()
{
	mockMouseState.windowEnterCount++;
}
void CMouseHandler::WindowLeave()
{
	mockMouseState.windowLeaveCount++;
}

namespace {

struct MouseInputFixture {
	StubConfigHandler stubConfig;
	ConfigHandler* prevConfig = nullptr;
	CGlobalRendering* prevRendering = nullptr;
	CMouseHandler* prevMouse = nullptr;
	CGlobalRendering renderingInstance;
	alignas(CMouseHandler) std::byte dummyMouseMem[sizeof(CMouseHandler)];

	MouseInputFixture()
		: prevConfig(configHandler)
		, prevRendering(globalRendering)
		, prevMouse(mouse)
	{
		configHandler = &stubConfig;
		globalRendering = &renderingInstance;
		memset(dummyMouseMem, 0, sizeof(dummyMouseMem));
		mouse = reinterpret_cast<CMouseHandler*>(dummyMouseMem);
		mockMouseState = MockMouseHandlerState{};

		globalRendering->winSizeX = 1440;
		globalRendering->winSizeY = 900;
		globalRendering->drawableSizeX = 2880;
		globalRendering->drawableSizeY = 1800;
		globalRendering->pixelScaleX = 2.0f;
		globalRendering->pixelScaleY = 2.0f;
		globalRendering->viewPosX = 0;
		globalRendering->viewPosY = 0;
		globalRendering->viewSizeX = 2880;
		globalRendering->viewSizeY = 1800;
		globalRendering->viewWindowOffsetY = 0;
	}

	~MouseInputFixture()
	{
		configHandler = prevConfig;
		globalRendering = prevRendering;
		mouse = prevMouse;
	}
};

} // namespace

bool ConfigHandler::Get(const std::string& key) const
{
	const std::string val = GetString(key);
	return (val == "1" || val == "true");
}

TEST_CASE("IMouseInput WindowToPixelCoords Direct Scaling", "[input][mouse][highdpi]")
{
	SECTION("1.0x standard display scaling") {
		const int2 coords = IMouseInput::WindowToPixelCoords(100, 200, 1.0f, 1.0f);
		CHECK(coords.x == 100);
		CHECK(coords.y == 200);

		const int2 origin = IMouseInput::WindowToPixelCoords(0, 0, 1.0f, 1.0f);
		CHECK(origin.x == 0);
		CHECK(origin.y == 0);
	}

	SECTION("2.0x macOS Retina display scaling") {
		const int2 coords = IMouseInput::WindowToPixelCoords(100, 200, 2.0f, 2.0f);
		CHECK(coords.x == 200);
		CHECK(coords.y == 400);

		const int2 corner = IMouseInput::WindowToPixelCoords(1440, 900, 2.0f, 2.0f);
		CHECK(corner.x == 2880);
		CHECK(corner.y == 1800);
	}

	SECTION("Fractional display scaling (1.25x and 1.5x) with subpixel rounding") {
		const int2 scaled15 = IMouseInput::WindowToPixelCoords(100, 200, 1.5f, 1.5f);
		CHECK(scaled15.x == 150);
		CHECK(scaled15.y == 300);

		// 101 * 1.5 = 151.5 -> rounds to 152
		// 103 * 1.5 = 154.5 -> rounds to 155
		const int2 rounded15 = IMouseInput::WindowToPixelCoords(101, 103, 1.5f, 1.5f);
		CHECK(rounded15.x == 152);
		CHECK(rounded15.y == 155);

		// 100 * 1.25 = 125, 200 * 1.25 = 250
		const int2 scaled125 = IMouseInput::WindowToPixelCoords(100, 200, 1.25f, 1.25f);
		CHECK(scaled125.x == 125);
		CHECK(scaled125.y == 250);
	}

	SECTION("Asymmetric scaling factors") {
		const int2 asymm = IMouseInput::WindowToPixelCoords(100, 100, 1.5f, 2.0f);
		CHECK(asymm.x == 150);
		CHECK(asymm.y == 200);
	}

	SECTION("Negative coordinates (multi-monitor offsets)") {
		const int2 neg = IMouseInput::WindowToPixelCoords(-100, -50, 2.0f, 2.0f);
		CHECK(neg.x == -200);
		CHECK(neg.y == -100);
	}
}

TEST_CASE("IMouseInput WindowToPixelDelta Scaling", "[input][mouse][highdpi]")
{
	SECTION("1.0x delta movements") {
		const int2 delta = IMouseInput::WindowToPixelDelta(5, -10, 1.0f, 1.0f);
		CHECK(delta.x == 5);
		CHECK(delta.y == -10);
	}

	SECTION("2.0x Retina delta movements") {
		const int2 delta = IMouseInput::WindowToPixelDelta(5, -10, 2.0f, 2.0f);
		CHECK(delta.x == 10);
		CHECK(delta.y == -20);
	}

	SECTION("Zero delta") {
		const int2 zero = IMouseInput::WindowToPixelDelta(0, 0, 2.0f, 2.0f);
		CHECK(zero.x == 0);
		CHECK(zero.y == 0);
	}

	SECTION("Fractional delta with rounding") {
		// 3 * 1.5 = 4.5 -> 5
		// 7 * 1.5 = 10.5 -> 11
		const int2 frac = IMouseInput::WindowToPixelDelta(3, 7, 1.5f, 1.5f);
		CHECK(frac.x == 5);
		CHECK(frac.y == 11);
	}
}

TEST_CASE("IMouseInput PixelToWindowCoords Inverse Scaling", "[input][mouse][highdpi]")
{
	SECTION("1.0x inverse") {
		const int2 win = IMouseInput::PixelToWindowCoords(200, 400, 1.0f, 1.0f);
		CHECK(win.x == 200);
		CHECK(win.y == 400);
	}

	SECTION("2.0x Retina inverse") {
		const int2 win = IMouseInput::PixelToWindowCoords(200, 400, 2.0f, 2.0f);
		CHECK(win.x == 100);
		CHECK(win.y == 200);

		const int2 corner = IMouseInput::PixelToWindowCoords(2880, 1800, 2.0f, 2.0f);
		CHECK(corner.x == 1440);
		CHECK(corner.y == 900);
	}

	SECTION("Round-trip fidelity") {
		const int2 orig(720, 450);
		const int2 pixel = IMouseInput::WindowToPixelCoords(orig.x, orig.y, 2.0f, 2.0f);
		const int2 roundTrip = IMouseInput::PixelToWindowCoords(pixel.x, pixel.y, 2.0f, 2.0f);
		CHECK(roundTrip.x == orig.x);
		CHECK(roundTrip.y == orig.y);
	}
}

TEST_CASE("IMouseInput High-DPI Safety Fallbacks", "[input][mouse][highdpi]")
{
	SECTION("Zero scale falls back safely to 1.0f without divide-by-zero") {
		const int2 toPixel = IMouseInput::WindowToPixelCoords(100, 100, 0.0f, 0.0f);
		CHECK(toPixel.x == 100);
		CHECK(toPixel.y == 100);

		const int2 toWindow = IMouseInput::PixelToWindowCoords(100, 100, 0.0f, 0.0f);
		CHECK(toWindow.x == 100);
		CHECK(toWindow.y == 100);
	}

	SECTION("Negative scale falls back safely to 1.0f") {
		const int2 toPixel = IMouseInput::WindowToPixelCoords(100, 100, -1.0f, -2.0f);
		CHECK(toPixel.x == 100);
		CHECK(toPixel.y == 100);

		const int2 toWindow = IMouseInput::PixelToWindowCoords(100, 100, -1.0f, -2.0f);
		CHECK(toWindow.x == 100);
		CHECK(toWindow.y == 100);
	}

	SECTION("Null globalRendering falls back safely to 1.0f") {
		CGlobalRendering* orig = globalRendering;
		globalRendering = nullptr;

		const int2 toPixel = IMouseInput::WindowToPixelCoords(150, 250);
		CHECK(toPixel.x == 150);
		CHECK(toPixel.y == 250);

		const int2 toWindow = IMouseInput::PixelToWindowCoords(150, 250);
		CHECK(toWindow.x == 150);
		CHECK(toWindow.y == 250);

		const int2 toDelta = IMouseInput::WindowToPixelDelta(10, 20);
		CHECK(toDelta.x == 10);
		CHECK(toDelta.y == 20);

		globalRendering = orig;
	}
}

TEST_CASE("IMouseInput Integration with CGlobalRendering pixelScale", "[input][mouse][highdpi]")
{
	MouseInputFixture fixture;

	SECTION("Picks up 2.0x Retina scale from globalRendering") {
		const int2 coords = IMouseInput::WindowToPixelCoords(500, 300);
		CHECK(coords.x == 1000);
		CHECK(coords.y == 600);

		const int2 delta = IMouseInput::WindowToPixelDelta(10, -5);
		CHECK(delta.x == 20);
		CHECK(delta.y == -10);

		const int2 win = IMouseInput::PixelToWindowCoords(1000, 600);
		CHECK(win.x == 500);
		CHECK(win.y == 300);
	}

	SECTION("Dynamically updates when globalRendering pixelScale changes") {
		fixture.renderingInstance.pixelScaleX = 1.25f;
		fixture.renderingInstance.pixelScaleY = 1.25f;

		const int2 coords = IMouseInput::WindowToPixelCoords(400, 800);
		CHECK(coords.x == 500);
		CHECK(coords.y == 1000);
	}
}

TEST_CASE("IMouseInput SDL Event Handling with Retina Scaling", "[input][mouse][highdpi]")
{
	MouseInputFixture fixture;
	IMouseInput testInput(false);

	SECTION("SDL_MOUSEMOTION scales position to physical pixels") {
		SDL_Event motionEvent;
		motionEvent.type = SDL_MOUSEMOTION;
		motionEvent.motion.x = 720;
		motionEvent.motion.y = 450;
		motionEvent.motion.xrel = 15;
		motionEvent.motion.yrel = -10;

		testInput.HandleSDLMouseEvent(motionEvent);

		CHECK(testInput.GetPos().x == 1440);
		CHECK(testInput.GetPos().y == 900);
		CHECK(mockMouseState.lastMoveX == 1440);
		CHECK(mockMouseState.lastMoveY == 900);
		CHECK(mockMouseState.lastMoveDx == 30);
		CHECK(mockMouseState.lastMoveDy == -20);
	}

	SECTION("SDL_MOUSEBUTTONDOWN scales click position to physical pixels") {
		SDL_Event btnDownEvent;
		btnDownEvent.type = SDL_MOUSEBUTTONDOWN;
		btnDownEvent.button.x = 100;
		btnDownEvent.button.y = 200;
		btnDownEvent.button.button = SDL_BUTTON_LEFT;

		testInput.HandleSDLMouseEvent(btnDownEvent);

		CHECK(testInput.GetPos().x == 200);
		CHECK(testInput.GetPos().y == 400);
		CHECK(mockMouseState.lastPressX == 200);
		CHECK(mockMouseState.lastPressY == 400);
		CHECK(mockMouseState.lastPressBtn == SDL_BUTTON_LEFT);
	}

	SECTION("SDL_MOUSEBUTTONUP scales release position to physical pixels") {
		SDL_Event btnUpEvent;
		btnUpEvent.type = SDL_MOUSEBUTTONUP;
		btnUpEvent.button.x = 300;
		btnUpEvent.button.y = 400;
		btnUpEvent.button.button = SDL_BUTTON_LEFT;

		testInput.HandleSDLMouseEvent(btnUpEvent);

		CHECK(testInput.GetPos().x == 600);
		CHECK(testInput.GetPos().y == 800);
		CHECK(mockMouseState.lastReleaseX == 600);
		CHECK(mockMouseState.lastReleaseY == 800);
		CHECK(mockMouseState.lastReleaseBtn == SDL_BUTTON_LEFT);
	}
}
