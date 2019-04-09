#ifndef VERMES_RESOURCE_LOCATOR_H
#define VERMES_RESOURCE_LOCATOR_H

#include <map>
#include <string>
#include <list>
#include <set>
#include <iostream>
#include <algorithm>
#include <stdexcept>
//#include <console.h> //For IStrCompare
#include "util/text.h"
#include "gusanos/allegro.h"
#include "Debug.h"
//Check version to fix "next is not a member of boost" error during compilation when Boost version is 1.67
//Credits: https://github.com/arvidn/libtorrent/pull/2929/commits/253ae7ebeaa8287877b7fad2d8b2c083e445340c
//NOTE: Perhaps we could include utility.hpp anyway, but it's apparently not needed, and using it is discouraged according to documentation
#include <boost/version.hpp>
#if BOOST_VERSION >= 106700
	#include <boost/next_prior.hpp>
#else
	#include <boost/utility.hpp>
#endif


template<class T, bool Cache = true, bool ReturnResource = true>
struct ResourceLocator
{
	ResourceLocator()
	{
	}
	
	struct BaseLoader
	{
		// Should return true and set name to the resource name 
		// if the file/folder specified by path can be loaded.
		// Otherwise, it should return false.
		virtual bool canLoad(std::string const& path, std::string& name) = 0;
		
		// Should load the resource located at path
		virtual bool load(T*, std::string const& path) = 0;
		
		virtual const char* getName() = 0;
		
		virtual std::string format() = 0;
		virtual std::string formatShort() = 0;

		virtual ~BaseLoader() { }
	};
	
	// Information about a resource found via refresh()
	struct ResourceInfo
	{
		ResourceInfo() : loader(0), cached(0) {}
		
		ResourceInfo(std::string const& path_, BaseLoader* loader_)
		: path(path_), loader(loader_), cached(0)
		{

		}
		
		~ResourceInfo()
		{
			delete cached;
		}
				
		std::string path; // Path to load from
		BaseLoader* loader;   // Loader to use
		T* cached;
	};
	
	typedef std::map<std::string, ResourceInfo, IStrCompare> NamedResourceMap;
	
	// Refreshes the internal resource list by
	// scanning the path list after resources that can
	// be loaded by any of the registered loaders.
	// Folders are tried as well. If a loader is found for
	// a folder, all the subfolders/files are ignored.
	void refresh();
	
	// Clears all resources and paths
	void clear()
	{
		m_paths.clear();
		m_namedResources.clear();
	}
	
	// Clears resources that can be safely removed
	void clearUncached()
	{
		for(typename NamedResourceMap::iterator i = m_namedResources.begin(); i != m_namedResources.end();)
		{
			typename NamedResourceMap::iterator next = boost::next(i);
			
			if(!i->second.cached)
			{
				m_namedResources.erase(i);
			}
			else
			{
				std::cout << "Resource " << i->first << " is cached, leaving it behind" << std::endl;
			}
			
			i = next;
		}
	}
	
	// Adds a path to the path list
	void addPath(std::string const& path)
	{
		//m_paths.insert(path);
		if(std::find(m_paths.begin(), m_paths.end(), path) == m_paths.end())
			m_paths.push_back(path);
	}
	
	// Loads the named resource
	bool load(T*, std::string const& name);
	
	void addResource(const std::string& name, const std::string& path, BaseLoader* loader);

	// Loads and returns the named resource or, if it was loaded already, returns a cached version
	T* load(std::string const& name);
	
	std::string const& getPathOf(std::string const& name);
	
	// Returns true if the named resource can be loaded
	bool exists(std::string const& name);

	bool canLoad(const std::string& file, std::string& name, BaseLoader*& loader);

	// Registers a new loader for this resource type
	void registerLoader(BaseLoader* loader)
	{
		m_loaders.push_back(loader);
	}
	
	NamedResourceMap const& getMap()
	{
		return m_namedResources;
	}
	
private:
	void refresh(std::string const& path);
	
	NamedResourceMap m_namedResources; //The resource list
	
	std::list<BaseLoader *> m_loaders; // Registered loaders
	std::list<std::string>     m_paths; // Paths to scan
};

template<class T, bool Cache, bool ReturnResource>
void ResourceLocator<T, Cache, ReturnResource>::addResource(const std::string& name, const std::string& path, BaseLoader* loader) {
	/*std::pair<typename NamedResourceMap::iterator, bool> r =*/ m_namedResources.insert(std::make_pair(name, ResourceInfo(path, loader)));
	/*
	 if(r.second)
	 {
	 notes << "Found resource: " << name << ", loader: " << loader->getName() << endl;
	 }
	 else
	 {
	 notes << "Duplicate resource: " << name << ", old path: " << r.first->second.path << ", new path: " << i->get() << endl;
	 }
	 */	
}

template<class T, bool Cache, bool ReturnResource>
bool ResourceLocator<T, Cache, ReturnResource>::canLoad(const std::string& file, std::string& name, BaseLoader*& loader) {
	// Try loaders until a working one is found					

	for(typename std::list<BaseLoader *>::iterator l = m_loaders.begin();
		l != m_loaders.end();
		++l)
	{
		if((*l)->canLoad(file, name))
		{
			loader = *l;
			return true;
		}
	}	
	
	return false;
}

template<class T, bool Cache, bool ReturnResource>
void ResourceLocator<T, Cache, ReturnResource>::refresh(std::string const& path)
{
	//notes << typeid(T).name() << ": Scanning: " << path << endl;
	for(Iterator<std::string>::Ref i = FileListIter(path); i->isValid(); i->next())
	{
		//notes << " . entry: " << i->get() << endl;
		std::string name;
		BaseLoader* loader = 0;
		
		if(canLoad(path + "/" + i->get(), name, loader))
		{			
			// We found a loader
			addResource(name, path + "/" + i->get(), loader);
		}
		else if(gusIsDirectory(path + "/" + i->get()))
		{
			// If no loader was found and this is a directory, scan it
			refresh(path + "/" + i->get());
		}
	}
}

template<class T, bool Cache, bool ReturnResource>
void ResourceLocator<T, Cache, ReturnResource>::refresh()
{
	clearUncached();
	
	for(std::list<std::string>::const_reverse_iterator p = m_paths.rbegin();
	    p != std::list<std::string>::const_reverse_iterator(m_paths.rend());
	    ++p)
	{
		refresh(*p);
	}
}

template<class T, bool Cache, bool ReturnResource>
bool ResourceLocator<T, Cache, ReturnResource>::load(T* dest, std::string const& name)
{
	typename NamedResourceMap::iterator i = m_namedResources.find(name);
	if(i == m_namedResources.end())
		return false;

	return i->second.loader->load(dest, i->second.path);
}

template<class T, bool Cache, bool ReturnResource>
T* ResourceLocator<T, Cache, ReturnResource>::load(std::string const& name)
{
	if(!ReturnResource)
		return 0;
		
	typename NamedResourceMap::iterator i = m_namedResources.find(name);
	if(i == m_namedResources.end())
		return 0;
	
	if(Cache && i->second.cached)
		return i->second.cached; //Return the cached version
	
	T* resource = new T();
	bool r = i->second.loader->load(resource, i->second.path);
	if(!r)
	{
		// The loader failed, delete the resource
		delete resource;
		return 0;
	}
	
	// Cache the loaded resource
	if(Cache)
		i->second.cached = resource;
	
	return resource;
}

template<class T, bool Cache, bool ReturnResource>
std::string const& ResourceLocator<T, Cache, ReturnResource>::getPathOf(std::string const& name)
{
	typename NamedResourceMap::iterator i = m_namedResources.find(name);
	if(i == m_namedResources.end())
		throw std::runtime_error("Resource does not exist");
		
	return i->second.path;
}

template<class T, bool Cache, bool ReturnResource>
bool ResourceLocator<T, Cache, ReturnResource>::exists(std::string const& name)
{
	typename NamedResourceMap::iterator i = m_namedResources.find(name);
	if(i == m_namedResources.end())
		return false;
		
	return true;
}


#endif //VERMES_RESOURCE_LOCATOR_H
