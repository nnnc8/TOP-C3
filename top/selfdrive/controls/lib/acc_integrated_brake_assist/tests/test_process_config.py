from openpilot.system.manager.process_config import managed_processes


def test_manager_starts_brake_assist_provider_processes():
  assert managed_processes["friday_traffic_intentd"].module == (
    "top.selfdrive.controls.lib.acc_integrated_brake_assist.traffic_intentd"
  )
  assert "friday_intersection_distanced" not in managed_processes
  assert "friday_route_eventd" not in managed_processes
  assert "friday_amap_routed" not in managed_processes
