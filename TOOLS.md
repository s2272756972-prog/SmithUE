# SmithUE Tool Reference

> **211 tools across 23 domains** — Full-stack AI editor automation for Unreal Engine 5.2

## Domain Overview

| Domain | Tools | Description |
|--------|-------|-------------|
| Blueprint | 33 | BP creation, nodes, functions, variables, components, DSL, health/diff/trace |
| Material | 20 | Materials, material instances, MPC, material functions |
| Niagara | 17 | Particle systems, emitters, modules, renderers, parameters |
| Asset | 14 | Asset CRUD, browser ops, content browser selection/navigation, AI texture generation |
| Analysis | 13 | Source analysis, dependency graphs, BP diagnostics, asset validation |
| Level | 11 | Level management, landscape, foliage |
| Environment | 11 | Post-process, fog, sky, lights, physics, collision, splines |
| PIE | 11 | Play-In-Editor: start/stop, actors, properties, console |
| Editor | 9 | Actor spawning, properties, post-process, project settings |
| Data | 8 | DataTables, structs, enums, data assets |
| Observation | 8 | Panels, editor state, actor properties, world outline |
| Interaction | 7 | Console/editor commands, undo/redo, key simulation |
| Animation | 7 | AnimMontage, AnimBlueprint, sections, notifies |
| Viewport | 6 | Camera control, screenshots, actor selection, view modes |
| Sequencer | 6 | LevelSequence creation, bindings, tracks, keyframes |
| Input | 6 | Enhanced Input: InputAction, InputMappingContext |
| System | 5 | Server connectivity, metrics, protocol info |
| Project | 4 | Project info, plugins, folders, source files |
| Curve | 4 | Curve assets (Float/LinearColor/Vector) + color atlas |
| UMG | 4 | Widget Blueprint creation, widget tree, properties |
| Debug | 3 | Blueprint breakpoints: set, clear, list |
| RenderTarget | 2 | Texture render targets |
| Physics | 2 | Physical materials (friction/restitution/density) |

---

## Blueprint

| Tool | Description |
|------|-------------|
| `bp_create` | Create a new Blueprint asset |
| `bp_add_function` | Add a function graph to a Blueprint |
| `bp_create_node` | Create a node inside a Blueprint graph |
| `bp_connect_pins` | Connect two Blueprint node pins |
| `bp_disconnect_pins` | Disconnect two Blueprint node pins |
| `bp_set_pin_default` | Set a Blueprint node pin default value |
| `bp_delete_node` | Delete a node from a Blueprint graph |
| `bp_add_variable` | Add a Blueprint member variable |
| `bp_remove_variable` | Remove a Blueprint member variable by name |
| `bp_add_component` | Add a component to a Blueprint SCS |
| `bp_remove_component` | Remove a component from a Blueprint SCS |
| `bp_set_component_property` | Set a property on a Blueprint SCS or inherited component template |
| `bp_bulk_set_component_property` | Bulk-set generic component template properties on own SCS components in one Blueprint (bp_path) or every Blueprint directly under a folder (folder_path, non-recursive). Supports dotted/indexed property_path (e.g. RelativeLocation.Z, BodyInstance.bSimulatePhysics, OverrideMaterials[0]) plus semantic setters for collision, StaticMesh, Material[i], PostProcessMaterial, and ChildActorClass. |
| `bp_set_component_collision` | Bulk-set collision (object type + per-channel responses) on StaticMeshComponent templates inside one Blueprint (bp_path) or every Blueprint directly under a folder (folder_path, non-recursive). Switches the component to a Custom profile, then applies object type and responses via proper engine setters. Skips components whose StaticMesh has no collision geometry unless disabled. |
| `bp_override_function` | Override a parent class function in a Blueprint (creates proper override graph with correct signature) |
| `bp_compile` | Compile a Blueprint |
| `bp_reparent` | Change the parent class of a Blueprint |
| `bp_copy_graph` | Copy a function graph from one Blueprint to another |
| `bp_remove_graph` | Remove a function graph or ubergraph page from a Blueprint |
| `bp_rename_graph` | Rename a function graph or event graph page in a Blueprint (updates all internal call references) |
| `bp_fixup_self_references` | Fix variable/function/component nodes to reference Self instead of a foreign parent class (use after bp_copy_graph across different class hierarchies) |
| `bp_fix_local_var_scope` | Fix stale local variable scope references in all function graphs (use after bp_rename_graph or bp_copy_graph when local variables show scope mismatch warnings) |
| `bp_get_summary` | Get Blueprint metadata summary |
| `bp_get_component_details` | Read component template properties (mobility, transform, absolute flags, physics, collision, visibility, mesh, materials) for a Blueprint. Covers own SCS + inherited components. Closes the gap where bp_get_summary only shows hierarchy. |
| `bp_get_class_members` | Get a Blueprint or native class's members grouped by owning class, with inheritance-chain attribution and token-conscious output controls. |
| `bp_health_check` | Aggregate Blueprint health diagnostics: compile messages, unconnected required pins, broken references, and orphan impure nodes. |
| `bp_diff` | Structural comparison of two Blueprints across parent, components, variables, functions, interfaces, and overrides. |
| `bp_trace_value` | Trace data-flow upstream or downstream from a node data pin in a Blueprint graph. |
| `bp_describe_graph` | Describe nodes in a Blueprint graph. mode: full(default)/compact/summary/node_pins/exec_chain. exec_chain mode follows exec pins from entry points (add entry_node param to start from specific N-id). |
| `bp_compile_code` | Compile Blueprint DSL into a Blueprint |
| `bp_batch_op` | Execute multiple Blueprint atomic operations in a single transaction. Supports op aliases (connect/link/disconnect/unlink/set_default/set_value/create/add_node/delete/remove_node). Max 50 ops. Partial commit: failures do not stop subsequent ops. |
| `bp_validate_code` | Validate Blueprint DSL syntax without compiling |
| `bp_search` | Search nodes in a Blueprint by name (substring, case-insensitive) and/or type (exact class name). Searches all graphs (event, function, macro). |

## Material

| Tool | Description |
|------|-------------|
| `create_material` | Create a new UMaterial asset |
| `get_material_info` | Get information about a material including its expressions |
| `add_material_expression` | Add a material expression node to a material |
| `connect_material_pins` | Connect material expression pins. Use dest_expression_index=-1 to connect to material output. |
| `compile_material` | Trigger material recompilation |
| `set_material_property` | Set material properties (domain, blend_mode, shading_model, two_sided, blendable_location) |
| `set_expression_property` | Set properties on a material expression node (e.g. Custom HLSL code, constant values, texture) |
| `create_mpc` | Create a new Material Parameter Collection asset |
| `add_mpc_scalar` | Add a scalar parameter to a Material Parameter Collection |
| `add_mpc_vector` | Add a vector parameter to a Material Parameter Collection |
| `set_mpc_value` | Update the default value of a scalar parameter in a Material Parameter Collection |
| `create_material_instance` | Create a new MaterialInstanceConstant asset from a parent material |
| `set_mi_scalar` | Set a scalar parameter override on a MaterialInstanceConstant |
| `set_mi_vector` | Set a vector parameter override on a MaterialInstanceConstant |
| `get_mi_info` | Get info about a MaterialInstanceConstant: parent name and all scalar/vector parameter overrides |
| `create_material_function` | Create a new UMaterialFunction asset |
| `get_material_function_info` | Get information about a material function including its expressions |
| `add_mf_expression` | Add a material expression node to a material function |
| `connect_mf_pins` | Connect material expression pins within a material function |
| `set_mf_expression_property` | Set properties on a material function expression node (Custom HLSL code, constant values, function input/output names, etc.) |

## Niagara

| Tool | Description |
|------|-------------|
| `create_niagara_system` | Create a new UNiagaraSystem asset at the given content path |
| `niagara_get_system_info` | Get information about a Niagara system including emitters and user parameters |
| `niagara_add_emitter` | Add a new emitter to an existing Niagara system |
| `niagara_add_emitter_from_template` | Add an emitter to a Niagara system from a specified template asset path |
| `niagara_set_emitter_property` | Set a property on a Niagara emitter. Supported properties: enabled (bool), local_space (bool) |
| `niagara_compile` | Compile a Niagara system and save the asset |
| `niagara_add_renderer` | Add a renderer (sprite, mesh, or ribbon) to an emitter |
| `niagara_set_renderer_property` | Set a property on a Niagara renderer. Supports Material (asset path), and UObject properties via reflection |
| `niagara_add_module` | Add a Niagara module script to an emitter's stack group |
| `niagara_set_module_input` | Set an input value on a Niagara module |
| `niagara_add_user_parameter` | Add a user parameter to a Niagara system for Blueprint interaction |
| `spawn_niagara_actor` | Spawn a NiagaraActor in the level with a given system asset, auto-activated |
| `niagara_static_switch` | Get or set static switch values on a Niagara module. Omit switch_name to list all switches. |
| `niagara_search_assets` | Search Niagara assets (systems and emitters) in the project via AssetRegistry |
| `niagara_delete_renderer` | Delete a renderer from an emitter by index |
| `niagara_delete_module` | Delete a module from an emitter's stack by function name |
| `niagara_delete_emitter` | Delete an emitter from a Niagara system by name |

## Asset

| Tool | Description |
|------|-------------|
| `list_assets` | List assets in a content folder |
| `find_asset` | Find assets by name wildcard pattern |
| `get_asset_info` | Get detailed information about a specific asset |
| `rename_asset` | Rename an asset to a new name within the same folder |
| `duplicate_asset` | Duplicate an asset to a new path |
| `delete_asset` | Delete an asset. Checks references first and returns them if found. Use force=true to delete anyway. |
| `move_asset` | Move an asset to a new path (different folder and/or name). Updates all references. |
| `asset_editor` | Open or close asset editors. Supports single or multiple assets. |
| `save_asset` | Save a single asset to disk |
| `save_all_dirty` | Save all dirty (modified) assets to disk |
| `get_content_browser_selection` | Get the folders and assets currently selected in the Content Browser |
| `sync_content_browser` | Navigate the Content Browser to a folder or asset and bring it to focus |
| `generate_texture` | Generate a texture from a text prompt using an external AI image generation API. Returns a task_id for polling. |
| `check_generation_task` | Check the status of an asynchronous texture generation task |

## Analysis

| Tool | Description |
|------|-------------|
| `analyze_module` | Analyze C++ source files in a directory and return a nomnoml relationship diagram (class inheritance, composition, includes). Supports large projects via directory-level scoping. |
| `analyze_dependencies` | Analyze Build.cs module dependencies and return a nomnoml dependency graph. |
| `analyze_blueprints` | Analyze Blueprint assets in a Content Browser path and return a nomnoml inheritance + component composition diagram. |
| `bp_get_compile_errors` | Compile a Blueprint and return compiler errors and warnings |
| `bp_refresh_all_nodes` | Reconstruct all nodes in a Blueprint |
| `bp_find_unconnected_pins` | Find unconnected Blueprint exec and data pins |
| `bp_fix_broken_references` | Remove non-existent Blueprint variable references and refresh nodes |
| `asset_get_references` | Get package dependencies for an asset |
| `asset_get_referencers` | Get packages that reference an asset |
| `asset_find_orphans` | Find assets in a folder with no referencers |
| `asset_get_dependency_tree` | Get a recursive asset dependency tree |
| `asset_validate` | Validate that asset paths resolve and load without errors |
| `map_check_errors` | Run map check on the active editor world |

## Level

| Tool | Description |
|------|-------------|
| `level_new` | Create a new level/map in the editor |
| `level_open` | Open an existing level/map |
| `level_save` | Save the current level |
| `level_get_info` | Get current level name, path, and actor count |
| `level_create_landscape` | Create a landscape in the current level |
| `level_set_landscape_material` | Set the material on all landscape proxies in the current level |
| `level_get_landscape_info` | Get landscape proxy component counts, dimensions, and materials |
| `level_add_foliage_type` | Add a static mesh foliage type to the current level |
| `level_paint_foliage` | Add foliage instances at explicit locations |
| `level_erase_foliage` | Remove foliage instances within a radius |
| `level_get_foliage_stats` | Count foliage types and instances in the current level |

## Environment

| Tool | Description |
|------|-------------|
| `env_set_post_process` | Configure post-process volume settings |
| `env_set_fog` | Set exponential height fog properties |
| `env_set_sky_atmosphere` | Set sky atmosphere parameters |
| `env_set_light` | Set directional/point/spot light properties |
| `env_set_physics` | Enable/disable physics simulation on an actor |
| `env_set_collision` | Set collision profile/preset on an actor |
| `env_get_physics_info` | Get physics/collision info for an actor |
| `env_create_spline` | Create a spline actor with specified points |
| `env_add_spline_point` | Add a point to an existing spline actor |
| `env_set_spline_point` | Modify a spline point position and tangent |
| `env_get_spline_info` | Get spline point count, length, closed state |

## PIE

| Tool | Description |
|------|-------------|
| `pie_teleport_actor` | Teleport an actor in the PIE world |
| `pie_spawn_actor` | Spawn an actor in the PIE world |
| `pie_destroy_actor` | Destroy an actor in the PIE world |
| `pie_get_property` | Get a property value from an actor via reflection |
| `pie_set_property` | Set a property value on an actor via reflection |
| `pie_get_game_state` | Get PIE running state, player location, actor count |
| `pie_list_actors` | List actors in the PIE world |
| `pie_console_command` | Execute a console command in the PIE world |
| `pie_start` | Start a Play-In-Editor session |
| `pie_stop` | Stop the active PIE session |
| `pie_is_active` | Check whether a PIE session is currently active and return its mode |

## Editor

| Tool | Description |
|------|-------------|
| `spawn_actor` | Spawn an actor in the current level |
| `get_all_actors` | List all actors in the current level |
| `set_actor_property` | Set a reflected property on an actor by label |
| `delete_actor` | Delete an actor from the current level by label |
| `add_postprocess_material` | Add a material to a PostProcessVolume's blendable list |
| `open_map` | Open a map asset in the Unreal Editor |
| `get_project_setting` | Read a project configuration setting from INI file |
| `set_project_setting` | Write a project configuration setting to INI file and flush to disk |
| `auto_layout_graph` | Auto-arrange nodes in any graph (Material, Blueprint, Niagara). Closes editor if open to prevent save conflicts. |

## Data

| Tool | Description |
|------|-------------|
| `create_data_asset` | Create a Data Asset instance of a UDataAsset subclass |
| `read_data_asset` | Read a Data Asset's class and all UPROPERTY values via reflection |
| `data_create_table` | Create a DataTable asset using a row struct |
| `data_add_row` | Add or replace a row in a DataTable from JSON object data |
| `data_read_table` | Read all rows or a single row from a DataTable |
| `data_import_json` | Import DataTable rows from a JSON string |
| `data_create_struct` | Create a UserDefinedStruct asset with typed fields |
| `data_create_enum` | Create a UserDefinedEnum asset with entries |

## Observation

| Tool | Description |
|------|-------------|
| `list_panels` | Lists all known editor panels and whether each is currently open. |
| `open_panel` | Opens (or focuses) a named editor panel tab. |
| `close_panel` | Closes a named editor panel tab if it is open. |
| `get_editor_state` | Returns a snapshot of the current editor state: PIE, simulation, selection, level, viewport. |
| `get_actor_property` | Read a reflected property value from an actor by label |
| `get_selected_actors` | Returns the currently selected actors with label, class, location, and rotation. |
| `get_world_outline` | Returns all actors in the level with parent-child hierarchy and folder info. |
| `take_blueprint_preview_screenshot` | Open a Blueprint in its editor and capture the SCS (Components) viewport as a PNG screenshot |

## Interaction

| Tool | Description |
|------|-------------|
| `execute_editor_command` | Execute a named Unreal Editor command by name via GEditor->Exec |
| `execute_console_command` | Execute a UE console command in the current editor world |
| `list_editor_commands` | List all registered FUICommandInfo entries across all input binding contexts |
| `undo` | Undo the last editor transaction |
| `redo` | Redo the last undone editor transaction |
| `simulate_key` | Simulate a key press (command-lookup first, Slate fallback) |
| `list_key_bindings` | List all registered key bindings (commands with active key chords) |

## Animation

| Tool | Description |
|------|-------------|
| `anim_create_montage` | Create an AnimMontage asset for a skeleton |
| `anim_read_montage` | Read montage sections, notifies, and slots |
| `anim_add_section` | Add a section to an AnimMontage |
| `anim_link_sections` | Link two montage sections for sequential playback |
| `anim_add_notify` | Add an anim notify at a time |
| `anim_create_blueprint` | Create an AnimBlueprint asset |
| `anim_read_blueprint` | Read AnimBP info (state machines, variables, skeleton) |

## Viewport

| Tool | Description |
|------|-------------|
| `set_viewport_camera` | Set the active editor viewport camera location, rotation, and/or FOV |
| `focus_on_actor` | Move the active viewport camera to focus on a named actor |
| `set_viewport_mode` | Set the active viewport projection mode (perspective or orthographic) |
| `get_viewport_info_detailed` | Get detailed active viewport info: camera, size, realtime state, view mode |
| `select_actors` | Select one or more actors in the level by label |
| `take_viewport_screenshot` | Capture the active editor viewport as a PNG file |

## Sequencer

| Tool | Description |
|------|-------------|
| `seq_create` | Create a LevelSequence asset |
| `seq_read` | Read sequence info (bindings, tracks, range) |
| `seq_add_binding` | Bind a world actor to a sequence |
| `seq_add_track` | Add a track to a binding (Transform, Float, Bool) |
| `seq_add_keyframe` | Add a keyframe to a track at a given time |
| `seq_set_range` | Set the playback range in frames |

## Input

| Tool | Description |
|------|-------------|
| `input_create_action` | Create an Enhanced Input Action asset (IA_*) |
| `input_create_mapping_context` | Create an Input Mapping Context asset (IMC_*) with optional key mappings |
| `input_find_actions` | Find InputAction and InputMappingContext assets in the project |
| `input_read_mapping_context` | Read an InputMappingContext to see its action-key mappings and modifiers |
| `input_edit_mapping_context` | Edit an InputMappingContext: add or remove key mappings |
| `input_delete_asset` | Delete an InputAction or InputMappingContext asset by name or path |

## System

| Tool | Description |
|------|-------------|
| `ping` | Test server connectivity |
| `list_tools` | List all available commands with schemas |
| `get_protocol_info` | Get protocol and transport information |
| `system_get_metrics` | Return current session command metrics |
| `system_reset_metrics` | Reset all command metrics counters |

## Project

| Tool | Description |
|------|-------------|
| `get_project_info` | Returns basic project and engine information (name, version, directories). |
| `list_plugins` | Lists all discovered plugins with name, version, enabled status, description, and category. |
| `create_folder` | Creates a content browser folder (e.g. /Game/MyFolder/SubFolder). |
| `get_source_files` | Lists source files recursively under a given path, filtered by extension. |

## Curve

| Tool | Description |
|------|-------------|
| `create_curve` | Create a curve asset (Float, LinearColor, or Vector) with optional keyframes |
| `read_curve` | Read a curve asset's type and keyframes |
| `create_curve_atlas` | Create a CurveLinearColorAtlas, optionally seeded with CurveLinearColor assets |
| `read_curve_atlas` | Read a CurveLinearColorAtlas: texture size and gradient curve paths |

## UMG

| Tool | Description |
|------|-------------|
| `create_widget_blueprint` | Create a new Widget Blueprint asset |
| `read_widget_blueprint` | Read the widget tree of an existing Widget Blueprint |
| `add_widget` | Add a widget to an existing Widget Blueprint's widget tree |
| `set_widget_property` | Set a property on a widget inside a Widget Blueprint via reflection |

## Debug

| Tool | Description |
|------|-------------|
| `bp_set_breakpoint` | Set/enable a breakpoint on a Blueprint node by NodeGuid. |
| `bp_clear_breakpoint` | Remove a breakpoint from a Blueprint node by NodeGuid. |
| `bp_list_breakpoints` | List all breakpoints in a Blueprint with their graph, node GUID, title, and enabled state. |

## RenderTarget

| Tool | Description |
|------|-------------|
| `create_render_target` | Create a TextureRenderTarget2D |
| `read_render_target` | Read a TextureRenderTarget2D: size and format |

## Physics

| Tool | Description |
|------|-------------|
| `create_physical_material` | Create a PhysicalMaterial with friction/restitution/density |
| `read_physical_material` | Read a PhysicalMaterial's friction/restitution/density |

