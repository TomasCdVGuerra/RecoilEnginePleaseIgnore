/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef PICTURE_H
#define PICTURE_H

#include <string>
#include <memory>

#include "GuiElement.h"

namespace gfx
{
	class ITexture;
	class IVertexBuffer;
}

namespace agui
{

	class Picture : public GuiElement
	{
	public:
		Picture(GuiElement *parent = NULL);
		~Picture();

		void Load(const std::string &file);

	private:
		virtual void DrawSelf();

		unsigned texture;
		std::unique_ptr<gfx::ITexture> backendTexture;
		std::unique_ptr<gfx::IVertexBuffer> backendVertexBuffer;
		std::unique_ptr<gfx::IVertexBuffer> backendIndexBuffer;
		std::string file;
	};

}

#endif
