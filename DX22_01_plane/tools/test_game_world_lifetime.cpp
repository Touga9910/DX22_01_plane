#include "../GameWorld.h"
#include "../GameObject.h"
#include <Windows.h>
#include <cassert>
#include <iostream>

struct Marker final : Component {};

int main()
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
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
	void* inaccessible = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
	assert(inaccessible != nullptr);
	assert(!other.Contains(reinterpret_cast<const Component*>(inaccessible)));
	VirtualFree(inaccessible, 0, MEM_RELEASE);
	std::cout << "GameWorld lifetime regression passed (2000 destruction cycles and inaccessible address)\n";
}
