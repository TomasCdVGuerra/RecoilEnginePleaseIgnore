/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <SDL_events.h>
#include "System/Input/InputHandler.h"

#include "System/type2.h"

class IMouseInput
{
public:
	static IMouseInput* GetInstance(bool relModeWarp);
	static void FreeInstance(IMouseInput*);

	IMouseInput() = default;
	IMouseInput(bool relModeWarp);
	virtual ~IMouseInput();

	virtual void InstallWndCallback() {}

	int2 GetPos() const { return mousepos; }

	bool SetPos(int2 pos);
	bool WarpPos(int2 pos);
	bool SetWarpPos(int2 pos) { return (SetPos(pos) && WarpPos(pos)); }

	bool HandleSDLMouseEvent(const SDL_Event& event);

	virtual void SetWMMouseCursor(void* wmcursor) {}

	// High-DPI coordinate translation helpers
	static int2 WindowToPixelCoords(int winX, int winY);
	static int2 WindowToPixelCoords(int winX, int winY, float scaleX, float scaleY);
	static int2 WindowToPixelDelta(int dx, int dy);
	static int2 WindowToPixelDelta(int dx, int dy, float scaleX, float scaleY);
	static int2 PixelToWindowCoords(int pixX, int pixY);
	static int2 PixelToWindowCoords(int pixX, int pixY, float scaleX, float scaleY);

protected:
	int2 mousepos;
	InputHandler::HandlerTokenT inputCon;
};

extern IMouseInput* mouseInput;
