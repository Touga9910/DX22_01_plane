"""Guard the ownership ordering that caused the reported exit-time use-after-free.
These source contracts supplement, rather than replace, a Windows exit test.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')


class TitleShutdownContractTests(unittest.TestCase):
    def test_unicode_window_uses_unicode_message_dispatch_even_in_mbcs_release(self):
        app = source('Application.cpp')
        for function in ('RegisterClassExW', 'CreateWindowExW', 'PeekMessageW', 'DispatchMessageW', 'DefWindowProcW'):
            self.assertIn(function + '(', app)
        self.assertNotIn('DefWindowProc(', app)
        self.assertNotIn('DispatchMessage(', app)

    def test_failed_renderer_does_not_start_game_or_imgui(self):
        init = source('Game.cpp').split('bool Game::Init()', 1)[1].split('void Game::Update', 1)[0]
        self.assertLess(init.index('FAILED(rendererResult)'), init.index('new Game()'))
        self.assertLess(init.index('return false;'), init.index('BallCardPresentation::LoadResources()'))
        self.assertIn('return Game::Init();', source('GameRuntime.cpp'))
        loop = source('Application.cpp').split('void Application::MainLoop()', 1)[1]
        self.assertLess(loop.index('if (!GameRuntime::Initialize()) return;'), loop.index('ImGui::CreateContext()'))

    def test_shutdown_invalidates_singleton_and_tolerates_repetition(self):
        shutdown = source('Game.cpp').split('void Game::Uninit()', 1)[1]
        self.assertLess(shutdown.index('if (m_Instance == nullptr) return;'), shutdown.index('m_RuntimeState->ResetScene()'))
        self.assertLess(shutdown.index('delete m_Instance;'), shutdown.index('m_Instance = nullptr;'))

    def test_scene_observers_are_destroyed_before_world_and_renderer(self):
        shutdown = source('Game.cpp').split('void Game::Uninit()', 1)[1]
        self.assertIn('m_RuntimeState->ResetScene()', shutdown)
        scene = shutdown.index('m_RuntimeState->ResetScene()')
        world = shutdown.index('m_RuntimeState->World().Clear()')
        gpu = shutdown.index('Renderer::Uninit()')
        singleton = shutdown.index('delete m_Instance')
        self.assertLess(scene, world)
        self.assertLess(world, gpu)
        self.assertLess(gpu, singleton)

    def test_title_rejects_a_component_freed_before_scene_cleanup(self):
        cleanup = source('TitleScene.cpp').split('void TitleScene::Uninit()', 1)[1]
        self.assertIn('if(GameRuntime::World().Contains(m_Table))m_Table->Release();', cleanup)
        self.assertLess(cleanup.index('m_Table=nullptr;'), cleanup.index('m_SceneGameObjects.clear();'))

    def test_table_releases_private_objects_before_component_destruction(self):
        header = source('TitleTable.h')
        implementation = source('TitleTable.cpp')
        self.assertIn('void OnDestroy() override { Release(); }', header)
        self.assertIn('TitleTable::~TitleTable(){Release();}', implementation)
        release = implementation.split('void TitleTable::Release()', 1)[1].split('void TitleTable::Awake()', 1)[0]
        self.assertIn('m_Physics.impact={};', release)
        self.assertIn('m_Ready=false;', release)
        self.assertIn('visual.reset()', release)
        self.assertIn('m_Table.reset()', release)
        self.assertIn('m_Frame.reset()', release)


if __name__ == '__main__':
    unittest.main()
