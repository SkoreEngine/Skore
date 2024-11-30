#include "ResourceEditor.hpp"

namespace Skore
{

    namespace
    {
        ResourceFile* project;
    }


    void ResourceFile::OnChange(ResourceObject* resourceObject)
    {

    }

    void ResourceFile::Load(ResourceObject* resourceObject)
    {

    }

    void ResourceEditor::AddPackage(StringView name, StringView directory)
    {

    }

    void ResourceEditor::SetProject(StringView name, StringView directory)
    {
        project = Alloc<ResourceFile>();
        project->currentVersion = 0;
        project->persistedVersion = 1;

        //foreach on files;


    }
}
