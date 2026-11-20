#include "../GameWorld.h"
#include "../GameObject.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#endif
#include <cassert>
#include <iostream>

struct Marker final : Component {};

int main()
{
	#ifdef _WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	#endif
	GameWorld world;
	assert(!world.Contains(static_cast<const Component*>(nullptr)));
	auto* object = world.Create("lifetime");
	auto* marker = object->AddComponent<Marker>();
	assert(world.Contains(marker));
	object->SetActive(false); marker->SetEnabled(false);
	assert(world.Contains(marker)); // ownership is independent of rendering/enabled state
	world.RequestDestroy(object);
	assert(!world.Contains(marker));
	world.RemoveDestroyed();
	assert(!world.Contains(marker)); // never dereference a freed component
	for (int i = 0; i < 2000; ++i)
	{
		auto* owner = world.Create("reuse");
		auto* component = owner->AddComponent<Marker>();
		assert(world.Contains(component));
		world.Clear();
		assert(!world.Contains(component));
	}
	GameWorld other;
	auto* foreign = other.Create("foreign")->AddComponent<Marker>();
	assert(!world.Contains(foreign));
	// An unregistered component must also be rejected.
	struct LiveOwnerProbe final : Component {};
	LiveOwnerProbe unattached;
	assert(!other.Contains(&unattached));
	#ifdef _WIN32
	void* inaccessible = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
	assert(inaccessible != nullptr);
	#else
	void* inaccessible = mmap(nullptr,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
	assert(inaccessible != MAP_FAILED);
	#endif
	assert(!other.Contains(reinterpret_cast<const Component*>(inaccessible)));
	#ifdef _WIN32
	VirtualFree(inaccessible, 0, MEM_RELEASE);
	#else
	munmap(inaccessible,4096);
	#endif
	std::cout << "GameWorld lifetime regression passed (2000 destruction cycles and inaccessible address)\n";
}
