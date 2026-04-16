#include "StdInc.h"

#include "ResourceManager.h"
#include "ServerResourceList.h"

#include <skyr/url.hpp>
#include <skyr/percent_encode.hpp>

namespace
{
bool IsValidResourceName(std::string_view resourceName)
{
	if (resourceName.empty() || resourceName == "." || resourceName == "..")
	{
		return false;
	}

	for (const unsigned char ch : resourceName)
	{
		if (ch < 0x20 || ch == 0x7F || ch == '/' || ch == '\\')
		{
			return false;
		}
	}

	return true;
}
}

class ServerResourceMounter : public fx::ResourceMounter
{
public:
	ServerResourceMounter(fx::ResourceManager* manager)
		: m_manager(manager)
	{
	}

	virtual bool HandlesScheme(const std::string& scheme) override
	{
		return (scheme == "file");
	}

	virtual pplx::task<fwRefContainer<fx::Resource>> LoadResource(const std::string& uri) override
	{
		auto resourceList = m_manager->GetComponent<fx::resources::ServerResourceList>();
		auto uriParsed = skyr::make_url(uri);

		fwRefContainer<fx::Resource> resource;

		if (uriParsed)
		{
			const auto pathRef = uriParsed->pathname();
			const auto fragRef = uriParsed->hash().substr(1);

			if (!pathRef.empty() && !fragRef.empty())
			{
				auto decodedPathRef = skyr::percent_decode(pathRef);
				auto decodedFragmentRef = skyr::percent_decode(fragRef);

				if (!decodedPathRef || !decodedFragmentRef)
				{
					resourceList->AddError(fx::resources::ScanMessageType::Error, fragRef, "invalid_uri", { "URI percent-decoding failed." });
					return pplx::task_from_result<fwRefContainer<fx::Resource>>(nullptr);
				}

#ifdef _WIN32
				std::string pr = decodedPathRef->substr(1);
#else
				std::string pr = *decodedPathRef;
#endif
				std::string resourceName = *decodedFragmentRef;

				if (!IsValidResourceName(resourceName))
				{
					resourceList->AddError(fx::resources::ScanMessageType::Error, resourceName.empty() ? fragRef : resourceName, "invalid_resource_name", {});
					return pplx::task_from_result<fwRefContainer<fx::Resource>>(nullptr);
				}

				std::string error;

				resource = m_manager->CreateResource(resourceName, this);
				if (!resource->LoadFrom(pr, &error))
				{
					// error matching LuaMetaDataLoader.cpp in citizen:resources:metadata:lua
					if (error == "Could not open resource metadata file - no such file.")
					{
						resourceList->AddError(fx::resources::ScanMessageType::Warning, resourceName, "no_manifest", {});
					}
					else
					{
						resourceList->AddError(fx::resources::ScanMessageType::Error, resourceName, "load_failed", { error });
					}

					m_manager->RemoveResource(resource);
					resource = nullptr;
				}
			}
			else
			{
				resourceList->AddError(
					fx::resources::ScanMessageType::Error,
					fragRef.empty() ? uri : fragRef,
					"invalid_uri",
					{ "URI is missing path or resource name fragment." });
			}
		}
		else
		{
			resourceList->AddError(fx::resources::ScanMessageType::Error, uri, "invalid_uri", { "Failed to parse resource URI." });
		}

		return pplx::task_from_result<fwRefContainer<fx::Resource>>(resource);
	}

private:
	fx::ResourceManager* m_manager;
};

DLL_EXPORT fwRefContainer<fx::ResourceMounter> MakeServerResourceMounter(const fwRefContainer<fx::ResourceManager>& resman)
{
	return new ServerResourceMounter(resman.GetRef());
}
