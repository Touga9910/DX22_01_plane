"""Guard the ownership ordering that caused the reported exit-time use-after-free.
These source contracts supplement, rather than replace, a Windows exit test.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')


class TitleShutdownContractTests(unittest.TestCase):
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
