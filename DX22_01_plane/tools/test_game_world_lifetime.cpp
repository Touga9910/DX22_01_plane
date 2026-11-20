#include "../GameWorld.h"
#include "../GameObject.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#endif
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#ifdef _WIN32
#include <crtdbg.h>
#endif

struct Marker final : Component {};

namespace
{
enum class Pass { Update, Fixed, Late, Draw };

struct Counter : Component
{
    int calls[4]{};
    void Update() override { ++calls[0]; }
    void FixedUpdate() override { ++calls[1]; }
    void LateUpdate() override { ++calls[2]; }
    void Draw() override { ++calls[3]; }
};

template<std::size_t Index>
struct ChildCounter final : Counter {};

struct Spawner final : Component
{
    GameWorld* world = nullptr;
    Pass pass = Pass::Update;
    bool components = false, spawned = false, fromStart = false;
    std::vector<Counter*> children;
    void Spawn()
    {
        if (spawned) return;
        spawned = true;
        if (components)
        {
            // AddComponent permits one instance per concrete type.
            [&]<std::size_t... Index>(std::index_sequence<Index...>)
            {
                (children.push_back(GetGameObject()->AddComponent<ChildCounter<Index>>()), ...);
            }(std::make_index_sequence<128>{});
            return;
        }
        for (int i = 0; i < 128; ++i)
        {
            auto* object = world->Create("spawned");
            children.push_back(object->AddComponent<Counter>());
        }
    }
    void Start() override { if (fromStart) Spawn(); }
    void Update() override { if (pass == Pass::Update) Spawn(); }
    void FixedUpdate() override { if (pass == Pass::Fixed) Spawn(); }
    void LateUpdate() override { if (pass == Pass::Late) Spawn(); }
    void Draw() override { if (pass == Pass::Draw) Spawn(); }
};

void CheckMutation()
{
    for (bool components : {false, true})
    for (Pass pass : {Pass::Update, Pass::Fixed, Pass::Late, Pass::Draw})
    for (bool fromStart : {false, true})
    {
        if (fromStart && pass != Pass::Update && pass != Pass::Fixed) continue;
        GameWorld world;
        auto* object = world.Create("spawner");
        auto* spawner = object->AddComponent<Spawner>();
        spawner->world = &world;
        spawner->components = components;
        spawner->pass = pass;
        spawner->fromStart = fromStart;
        auto* tail = (components ? object : world.Create("tail"))->AddComponent<Counter>();
        const auto run = [&]
        {
            switch (pass)
            {
            case Pass::Update: world.Update(); break;
            case Pass::Fixed: world.FixedUpdate(); break;
            case Pass::Late: world.LateUpdate(); break;
            case Pass::Draw: world.Draw(); break;
            }
        };
        run();
        const int index = static_cast<int>(pass);
        assert(tail->calls[index] == 1);
        assert(spawner->children.size() == 128);
        for (const auto* child : spawner->children) assert(child->calls[index] == 0);
        run();
        assert(tail->calls[index] == 2);
        for (const auto* child : spawner->children) assert(child->calls[index] == 1);
    }
    std::cout << "World/Object mutation regression passed (12 generation cases)\n";
}
}

int main(int argc, char** argv)
{
	#ifdef _WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	#endif
    if (argc > 1 && std::string_view(argv[1]) == "--mutation")
    {
        CheckMutation();
        return 0;
    }
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
    CheckMutation();
}
