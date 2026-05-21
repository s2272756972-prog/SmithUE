# SmithUE Tool Reference

> **174 tools across 18 domains** — Full-stack AI editor automation for Unreal Engine 5.2

## Domain Overview

| Domain | Tools | Description |
|--------|-------|-------------|
| System | 5 | Server connectivity, session management |
| Project | 4 | Project info, plugins, folders, source files |
| Material | 20 | Materials, material instances, MPC, material functions |
| Asset | 12 | Asset CRUD, browser operations, AI texture generation |
| Editor | 8 | Actor spawning, properties, post-process, project settings, graph layout |
| Interaction | 7 | Console/editor commands, undo/redo, key simulation, key bindings |
| Blueprint | 17 | BP creation, nodes, functions, variables, components, DSL compiler |
| Viewport | 6 | Camera control, screenshots, actor selection, view modes |
| Observation | 7 | Panels, editor state, actor properties, world outline |
| Analysis | 13 | Source analysis, dependency graphs, BP diagnostics, asset validation |
| Niagara | 17 | Particle system creation, emitters, modules, renderers, parameters |
| Level | 11 | Level management, landscape, foliage |
| Data | 6 | DataTables, UserDefinedStructs, UserDefinedEnums |
| Sequencer | 6 | LevelSequence creation, bindings, tracks, keyframes |
| Environment | 11 | Post-process, fog, sky, lights, physics, collision, splines |
| PIE | 11 | Play-In-Editor: start/stop, actors, properties, console |
| Animation | 7 | AnimMontage, AnimBlueprint, sections, notifies |
| Input | 6 | Enhanced Input: InputAction, InputMappingContext |

---

## System (5 tools)

| Tool | Description |
|------|-------------|
| `ping` | Test server connectivity |
| `list_tools` | List all available commands with schemas |
| `get_protocol_info` | Get protocol and transport information |
| `register_session` | Register an MCP client session |
| `unregister_session` | Unregister an MCP client session |

## Project (4 tools)

| Tool | Description |
|------|-------------|
| `get_project_info` | Project name, version, directories |
| `list_plugins` | All plugins with status |
| `create_folder` | Create a content browser folder |
| `get_source_files` | List source files by extension |

## Material (20 tools)

| Tool | Description |
|------|-------------|
| `create_material` | Create a new UMaterial asset |
| `get_material_info` | Get material expressions and structure |
| `add_material_expression` | Add expression node to material |
| `connect_material_pins` | Connect expression pins |
| `compile_material` | Trigger material recompilation |
| `set_material_property` | Set domain, blend mode, shading model |
| `set_expression_property` | Set node properties (Custom HLSL, constants, textures) |
| `create_mpc` | Create Material Parameter Collection |
| `add_mpc_scalar` | Add scalar parameter to MPC |
| `add_mpc_vector` | Add vector parameter to MPC |
| `set_mpc_value` | Update MPC parameter default value |
| `create_material_instance` | Create MaterialInstanceConstant |
| `set_mi_scalar` | Set scalar parameter on MI |
| `set_mi_vector` | Set vector parameter on MI |
| `get_mi_info` | Get MI parent and parameter overrides |
| `create_material_function` | Create UMaterialFunction |
| `get_material_function_info` | Get material function expressions |
| `add_mf_expression` | Add expression to material function |
| `connect_mf_pins` | Connect pins within material function |
| `set_mf_expression_property` | Set properties on MF expression node |

## Asset (12 tools)

| Tool | Description |
|------|-------------|
| `list_assets` | List assets in a content folder |
| `find_asset` | Find assets by name pattern |
| `get_asset_info` | Get detailed asset information |
| `rename_asset` | Rename an asset |
| `duplicate_asset` | Duplicate an asset |
| `delete_asset` | Delete an asset (with reference check) |
| `move_asset` | Move asset to new path |
| `asset_editor` | Open/close asset editors |
| `save_asset` | Save a single asset |
| `save_all_dirty` | Save all modified assets |
| `generate_texture` | Generate texture via AI API |
| `check_generation_task` | Check async texture generation status |

## Editor (8 tools)

| Tool | Description |
|------|-------------|
| `spawn_actor` | Spawn an actor in the level |
| `get_all_actors` | List all actors in the level |
| `set_actor_property` | Set a reflected property on an actor |
| `delete_actor` | Delete an actor by label |
| `add_postprocess_material` | Add material to PostProcessVolume |
| `get_project_setting` | Read project INI config value |
| `set_project_setting` | Write project INI config value |
| `auto_layout_graph` | Auto-arrange graph nodes |

## Interaction (7 tools)

| Tool | Description |
|------|-------------|
| `execute_editor_command` | Execute named editor command |
| `execute_console_command` | Execute UE console command |
| `list_editor_commands` | List all registered FUICommandInfo |
| `undo` | Undo last editor transaction |
| `redo` | Redo last undone transaction |
| `simulate_key` | Simulate a key press |
| `list_key_bindings` | List registered key bindings |

## Blueprint (17 tools)

| Tool | Description |
|------|-------------|
| `bp_create` | Create a new Blueprint asset (supports C++ class, BP class name `_C`, or BP path `/Game/...` as parent) |
| `bp_add_function` | Add a function graph |
| `bp_create_node` | Create a node in a graph (supports `Class::Function` shorthand for CallFunction nodes) |
| `bp_connect_pins` | Connect two node pins |
| `bp_set_pin_default` | Set pin default value |
| `bp_delete_node` | Delete a node from a graph |
| `bp_add_variable` | Add a member variable |
| `bp_add_component` | Add a component to SCS (supports `parent` param for hierarchy attachment) |
| `bp_remove_component` | Remove a component from SCS (children reparented automatically) |
| `bp_set_component_property` | Set property on component template |
| `bp_override_function` | Override a parent class function |
| `bp_compile` | Compile a Blueprint |
| `bp_get_summary` | Get Blueprint metadata (includes component hierarchy with parent/children) |
| `bp_describe_graph` | Describe nodes and connections |
| `bp_compile_code` | Compile Blueprint DSL |
| `bp_batch_op` | Execute multiple atomic operations |
| `bp_validate_code` | Validate Blueprint DSL syntax |

## Viewport (6 tools)

| Tool | Description |
|------|-------------|
| `set_viewport_camera` | Set viewport camera transform/FOV |
| `focus_on_actor` | Focus viewport on an actor |
| `set_viewport_mode` | Set projection mode |
| `get_viewport_info_detailed` | Get full viewport state |
| `select_actors` | Select actors by label |
| `take_viewport_screenshot` | Capture viewport as PNG |

## Observation (7 tools)

| Tool | Description |
|------|-------------|
| `list_panels` | List all editor panels |
| `open_panel` | Open/focus a panel tab |
| `close_panel` | Close a panel tab |
| `get_editor_state` | Snapshot of editor state |
| `get_actor_property` | Read reflected property from actor |
| `get_selected_actors` | Get currently selected actors |
| `get_world_outline` | All actors with hierarchy and folders |

## Analysis (13 tools)

| Tool | Description |
|------|-------------|
| `analyze_module` | C++ source → nomnoml class diagram |
| `analyze_dependencies` | Build.cs → nomnoml dependency graph |
| `analyze_blueprints` | BP assets → nomnoml inheritance diagram |
| `bp_get_compile_errors` | Compile BP and return errors |
| `bp_refresh_all_nodes` | Reconstruct all nodes in BP |
| `bp_find_unconnected_pins` | Find unconnected exec/data pins |
| `bp_fix_broken_references` | Remove broken variable references |
| `asset_get_references` | Get asset's package dependencies |
| `asset_get_referencers` | Get packages that reference an asset |
| `asset_find_orphans` | Find unreferenced assets |
| `asset_get_dependency_tree` | Recursive dependency tree |
| `asset_validate` | Validate asset paths load correctly |
| `map_check_errors` | Run map check on active world |

## Niagara (17 tools)

| Tool | Description |
|------|-------------|
| `create_niagara_system` | Create a NiagaraSystem asset |
| `niagara_get_system_info` | Get system emitters and parameters |
| `niagara_add_emitter` | Add empty emitter |
| `niagara_add_emitter_from_template` | Add emitter from template asset |
| `niagara_set_emitter_property` | Set emitter enabled/local_space |
| `niagara_compile` | Compile and save system |
| `niagara_add_renderer` | Add sprite/mesh/ribbon renderer |
| `niagara_set_renderer_property` | Set renderer material/properties |
| `niagara_add_module` | Add module to stack group |
| `niagara_set_module_input` | Set module input value |
| `niagara_add_user_parameter` | Add user parameter for BP interaction |
| `spawn_niagara_actor` | Spawn NiagaraActor in level |
| `niagara_static_switch` | Get/set static switch values |
| `niagara_search_assets` | Search Niagara assets by pattern |
| `niagara_delete_renderer` | Delete renderer by index |
| `niagara_delete_module` | Delete module from stack |
| `niagara_delete_emitter` | Delete emitter from system |

## Level (11 tools)

| Tool | Description |
|------|-------------|
| `level_new` | Create a new level |
| `level_open` | Open an existing level |
| `level_save` | Save the current level |
| `level_get_info` | Get level name, path, actor count |
| `level_create_landscape` | Create a landscape |
| `level_set_landscape_material` | Set landscape material |
| `level_get_landscape_info` | Get landscape dimensions and materials |
| `level_add_foliage_type` | Add a foliage type |
| `level_paint_foliage` | Add foliage instances at locations |
| `level_erase_foliage` | Remove foliage within radius |
| `level_get_foliage_stats` | Count foliage types and instances |

## Data (6 tools)

| Tool | Description |
|------|-------------|
| `data_create_table` | Create a DataTable asset |
| `data_add_row` | Add/replace a row in DataTable |
| `data_read_table` | Read rows from DataTable |
| `data_import_json` | Import DataTable from JSON string |
| `data_create_struct` | Create UserDefinedStruct with fields |
| `data_create_enum` | Create UserDefinedEnum with entries |

## Sequencer (6 tools)

| Tool | Description |
|------|-------------|
| `seq_create` | Create a LevelSequence asset |
| `seq_read` | Read sequence bindings and tracks |
| `seq_add_binding` | Bind a world actor to sequence |
| `seq_add_track` | Add track to a binding |
| `seq_add_keyframe` | Add keyframe at time |
| `seq_set_range` | Set playback range in frames |

## Environment (11 tools)

| Tool | Description |
|------|-------------|
| `env_set_post_process` | Configure post-process settings |
| `env_set_fog` | Set exponential height fog |
| `env_set_sky_atmosphere` | Set sky atmosphere parameters |
| `env_set_light` | Set light properties |
| `env_set_physics` | Enable/disable physics on actor |
| `env_set_collision` | Set collision profile |
| `env_get_physics_info` | Get physics/collision info |
| `env_create_spline` | Create spline with points |
| `env_add_spline_point` | Add point to spline |
| `env_set_spline_point` | Modify spline point position |
| `env_get_spline_info` | Get spline info |

## PIE (11 tools)

| Tool | Description |
|------|-------------|
| `pie_start` | Start Play-In-Editor session |
| `pie_stop` | Stop active PIE session |
| `pie_is_active` | Check if PIE is running and its mode |
| `pie_teleport_actor` | Teleport actor in PIE world |
| `pie_spawn_actor` | Spawn actor in PIE world |
| `pie_destroy_actor` | Destroy actor in PIE world |
| `pie_get_property` | Get property from PIE actor |
| `pie_set_property` | Set property on PIE actor |
| `pie_get_game_state` | Get PIE state, player location |
| `pie_list_actors` | List actors in PIE world |
| `pie_console_command` | Execute console command in PIE |

## Animation (7 tools)

| Tool | Description |
|------|-------------|
| `anim_create_montage` | Create AnimMontage for skeleton |
| `anim_read_montage` | Read montage sections/notifies |
| `anim_add_section` | Add section to montage |
| `anim_link_sections` | Link sections for playback |
| `anim_add_notify` | Add anim notify at time |
| `anim_create_blueprint` | Create AnimBlueprint |
| `anim_read_blueprint` | Read AnimBP info |

## Input (6 tools)

| Tool | Description |
|------|-------------|
| `input_create_action` | Create Enhanced Input Action (IA_*) |
| `input_create_mapping_context` | Create Input Mapping Context (IMC_*) |
| `input_find_actions` | Find InputAction/IMC assets |
| `input_read_mapping_context` | Read IMC mappings and modifiers |
| `input_edit_mapping_context` | Add/remove key mappings in IMC |
| `input_delete_asset` | Delete InputAction or IMC asset |

---

## Changelog

### v0.2.0 — Domain Expansion

**Added domains (8 new):**
- Level (11 tools) — Landscape, foliage, level management
- Analysis (13 tools) — Source analysis, dependency graphs, BP diagnostics
- Data (6 tools) — DataTables, UserDefinedStructs, UserDefinedEnums
- Sequencer (6 tools) — LevelSequence creation and keyframing
- Environment (11 tools) — Post-process, fog, sky, lights, physics, splines
- PIE (11 tools) — Play-In-Editor runtime control and inspection
- Animation (7 tools) — AnimMontage and AnimBlueprint management
- Input (6 tools) — Enhanced Input system asset creation

**Removed duplicates:**
- `get_viewport_info` (Editor) → use `get_viewport_info_detailed` (Viewport)
- `get_level_info` (Observation) → use `level_get_info` (Level)
- `get_project_settings` (Project) → use `get_project_setting` (Editor)

**Reorganized:**
- Moved PIE control tools (`start_pie`, `stop_pie`, `is_pie_active`) from Interaction → PIE domain (renamed to `pie_start`, `pie_stop`, `pie_is_active`)
