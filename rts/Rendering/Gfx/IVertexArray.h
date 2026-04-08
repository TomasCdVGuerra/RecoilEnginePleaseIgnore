/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

namespace gfx
{

    class IVertexArray
    {
    public:
        virtual ~IVertexArray() = default;

        virtual void Bind() = 0;
        virtual void Unbind() = 0;
    };

} // namespace gfx
