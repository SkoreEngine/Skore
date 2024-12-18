#pragma once

#include "Skore/Core/StringView.hpp"
#include "Skore/Core/String.hpp"

namespace Skore::Path
{
    inline String Parent(const StringView& path)
    {
        String parentPath{};
        bool foundSeparator = false;
        auto it = path.end();
        do
        {
            if (foundSeparator)
            {
                parentPath.Insert(parentPath.begin(), *it);
            }
            if (*it == SK_PATH_SEPARATOR)
            {
                foundSeparator = true;
            }
            if (it == path.begin())
            {
                return parentPath;
            }
            it--;
        } while (true);
    }

    inline StringView Extension(const StringView& path)
    {
        auto it = path.end();
        while (it != path.begin())
        {
            it--;
            if (*it == '.')
            {
                return StringView{it, (usize) (path.end() - it)};
            }
            else if (*it == SK_PATH_SEPARATOR)
            {
                break;
            }
        }
        return {};
    }

    template<typename ...T>
    String Join(const T& ... paths)
    {
        String retPath{};
        auto append = [&](StringView path)
        {
            if (path.Empty()) return;

            char first = path[0];
            if (first != '.' && first != '/' && first != '\\' && !retPath.Empty())
            {
                char last = retPath[retPath.Size() - 1];
                if (last != '/' && last != '\\')
                {
                    retPath.Append(SK_PATH_SEPARATOR);
                }
            }

            for (int i = 0; i < path.Size(); ++i)
            {
                if (path[i] == '/' || path[i] == '\\')
                {
                    if (i < path.Size() - 1)
                    {
                        retPath.Append(SK_PATH_SEPARATOR);
                    }
                }
                else
                {
                    retPath.Append(path[i]);
                }
            }
        };
        (append(StringView{paths}), ...);
        return retPath;
    }

    inline String Name(const StringView& path)
    {
        bool hasExtension = !Extension(path).Empty();

        auto it = path.end();
        if (it == path.begin()) return {};
        it--;

        //if the last char is a separator
        //like /path/folder/
        if (*it == SK_PATH_SEPARATOR)
        {
            it--;
        }

        String name{};
        bool found{};

        if (!hasExtension)
        {
            found = true;
        }

        while (it != path.begin())
        {
            if (found && *it == SK_PATH_SEPARATOR)
            {
                return name;
            }
            if (found)
            {
                name.Insert(name.begin(), *it);
            }
            if (*it == '.' || *it == SK_PATH_SEPARATOR)
            {
                found = true;
            }
            it--;
        }
        return path;
    }
}