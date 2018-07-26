#ifndef SLIC3RXS
#include "PrintGCode.hpp"
#include "PrintConfig.hpp"

#include <ctime>
#include <iostream>

namespace Slic3r {
void
PrintGCode::output()
{
    auto& gcodegen {this->_gcodegen};
    auto& fh {this->fh};
    auto& print {this->_print};
    const auto& config {this->config};
    const auto extruders {print.extruders()};

    // Write information about the generator.
    time_t rawtime; tm * timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    
    fh << "; generated by Slic3r " << SLIC3R_VERSION << " on ";
    fh << asctime(timeinfo) << "\n";
    fh << "; Git Commit: " << BUILD_COMMIT << "\n\n";

    // Writes notes (content of all Settings tabs -> Notes)
    fh << gcodegen.notes();

    // Write some terse information on the slicing parameters.
    auto& first_object {*(this->objects.at(0))};
    auto layer_height {first_object.config.layer_height.getFloat()};

    for (auto* region : print.regions) {
        {
            auto flow {region->flow(frExternalPerimeter, layer_height, false, false, -1, first_object)};
            auto vol_speed {flow.mm3_per_mm() * region->config.get_abs_value("external_perimeter_speed")};
            if (config.max_volumetric_speed.getInt() > 0)
                vol_speed = std::min(vol_speed, config.max_volumetric_speed.getFloat());
            fh << "; external perimeters extrusion width = ";
            fh << std::fixed << std::setprecision(2) << flow.width << "mm ";
            fh << "(" << vol_speed << "mm^3/s)\n";
        }
        {
            auto flow {region->flow(frPerimeter, layer_height, false, false, -1, first_object)};
            auto vol_speed {flow.mm3_per_mm() * region->config.get_abs_value("perimeter_speed")};
            if (config.max_volumetric_speed.getInt() > 0)
                vol_speed = std::min(vol_speed, config.max_volumetric_speed.getFloat());
            fh << "; perimeters extrusion width = ";
            fh << std::fixed << std::setprecision(2) << flow.width << "mm ";
            fh << "(" << vol_speed << "mm^3/s)\n";
        }
        {
            auto flow {region->flow(frInfill, layer_height, false, false, -1, first_object)};
            auto vol_speed {flow.mm3_per_mm() * region->config.get_abs_value("infill_speed")};
            if (config.max_volumetric_speed.getInt() > 0)
                vol_speed = std::min(vol_speed, config.max_volumetric_speed.getFloat());
            fh << "; infill extrusion width = ";
            fh << std::fixed << std::setprecision(2) << flow.width << "mm ";
            fh << "(" << vol_speed << "mm^3/s)\n";
        }
        {
            auto flow {region->flow(frSolidInfill, layer_height, false, false, -1, first_object)};
            auto vol_speed {flow.mm3_per_mm() * region->config.get_abs_value("solid_infill_speed")};
            if (config.max_volumetric_speed.getInt() > 0)
                vol_speed = std::min(vol_speed, config.max_volumetric_speed.getFloat());
            fh << "; solid infill extrusion width = ";
            fh << std::fixed << std::setprecision(2) << flow.width << "mm ";
            fh << "(" << vol_speed << "mm^3/s)\n";
        }
        {
            auto flow {region->flow(frTopSolidInfill, layer_height, false, false, -1, first_object)};
            auto vol_speed {flow.mm3_per_mm() * region->config.get_abs_value("top_solid_infill_speed")};
            if (config.max_volumetric_speed.getInt() > 0)
                vol_speed = std::min(vol_speed, config.max_volumetric_speed.getFloat());
            fh << "; top solid infill extrusion width = ";
            fh << std::fixed << std::setprecision(2) << flow.width << "mm ";
            fh << "(" << vol_speed << "mm^3/s)\n";
        }
        if (print.has_support_material()) {
            auto flow {first_object._support_material_flow()};
            auto vol_speed {flow.mm3_per_mm() * first_object.config.get_abs_value("support_material_speed")};
            if (config.max_volumetric_speed.getInt() > 0)
                vol_speed = std::min(vol_speed, config.max_volumetric_speed.getFloat());
            fh << "; support material extrusion width = ";
            fh << std::fixed << std::setprecision(2) << flow.width << "mm ";
            fh << "(" << vol_speed << "mm^3/s)\n";
        }
        if (print.config.first_layer_extrusion_width.getFloat() > 0) {
            auto flow {region->flow(frPerimeter, layer_height, false, false, -1, first_object)};
//          auto vol_speed {flow.mm3_per_mm() * print.config.get_abs_value("first_layer_speed")};
//          if (config.max_volumetric_speed.getInt() > 0)
//              vol_speed = std::min(vol_speed, config.max_volumetric_speed.getFloat());
            fh << "; first layer extrusion width = ";
            fh << std::fixed << std::setprecision(2) << flow.width << "mm ";
//          fh << "(" << vol_speed << "mm^3/s)\n";
        }

        fh << std::endl;
    }
    // Prepare the helper object for replacing placeholders in custom G-Code and output filename
    print.placeholder_parser.update_timestamp();

    // GCode sets this automatically when change_layer() is called, but needed for skirt/brim as well
    gcodegen.first_layer = true;

    // disable fan
    if (config.cooling.getBool() && config.disable_fan_first_layers.getInt() > 0) {
        fh << gcodegen.writer.set_fan(0,1) << "\n";
    }

    // set bed temperature
    auto bed_temp_regex { std::regex("M(?:190|140)", std::regex_constants::icase)};
    auto ex_temp_regex { std::regex("M(?:109|104)", std::regex_constants::icase)};
    auto temp{config.first_layer_bed_temperature.getFloat()};
    if (config.has_heatbed && temp > 0 && std::regex_search(config.start_gcode.getString(), bed_temp_regex)) {
        fh << gcodegen.writer.set_bed_temperature(temp, 1);
    }
    
    // Set extruder(s) temperature before and after start gcode.
    auto include_start_extruder_temp {!std::regex_search(config.start_gcode.getString(), ex_temp_regex)};
    for(const auto& start_gcode : config.start_filament_gcode.values) {
        include_start_extruder_temp = include_start_extruder_temp && !std::regex_search(start_gcode, ex_temp_regex);
    }

    auto include_end_extruder_temp {!std::regex_search(config.end_gcode.getString(), ex_temp_regex)};
    for(const auto& end_gcode : config.end_filament_gcode.values) {
        include_end_extruder_temp = include_end_extruder_temp && !std::regex_search(end_gcode, ex_temp_regex);
    }

    if (include_start_extruder_temp) this->_print_first_layer_temperature(0);

    // Apply gcode math to start and end gcode
    fh << apply_math(gcodegen.placeholder_parser->process(config.start_gcode.value));

    for(const auto& start_gcode : config.start_filament_gcode.values) {
        fh << apply_math(gcodegen.placeholder_parser->process(start_gcode));
    }
    
    if (include_start_extruder_temp) this->_print_first_layer_temperature(1);


    // Set other general things (preamble)
    fh << gcodegen.preamble();

    // initialize motion planner for object-to-object travel moves
    if (config.avoid_crossing_perimeters.getBool()) {

        // compute the offsetted convex hull for each object and repeat it for each copy
        Polygons islands_p {};
        for (auto object : this->objects) {
            Polygons polygons {};
            // Add polygons that aren't just thin walls.
            for (auto layer : object->layers) {
                const auto& slice {ExPolygons(layer->slices)};
                std::for_each(slice.cbegin(), slice.cend(), [&polygons] (const ExPolygon& a) { polygons.emplace_back(a.contour); });
            }
            
            if (polygons.size() == 0) continue;

            for (auto copy : object->_shifted_copies) {
                Polygons copy_islands_p {polygons};
                std::for_each(copy_islands_p.begin(), copy_islands_p.end(), [copy] (Polygon& obj) { obj.translate(copy); });
                islands_p.insert(islands_p.cend(), copy_islands_p.begin(), copy_islands_p.end());
            }
        }
        
        gcodegen.avoid_crossing_perimeters.init_external_mp(union_ex(islands_p));
    }

    // Calculate wiping points if needed.
    if (config.ooze_prevention && extruders.size() > 1) {
    }

    // Set initial extruder only after custom start gcode
    fh << gcodegen.set_extruder(*(extruders.begin()));

    // Do all objects for each layer.

    if (config.complete_objects) {
    } else {
        // order objects using a nearest neighbor search
        std::vector<Points::size_type> obj_idx {};
        Points p;
        for (const auto obj : this->objects ) 
            p.emplace_back(obj->_shifted_copies.at(0));
        Geometry::chained_path(p, obj_idx);

        std::vector<size_t> z;
        z.reserve(100); // preallocate with 100 layers
        std::map<coord_t, LayerPtrs> layers {};
        for (size_t idx = 0U; idx < print.objects.size(); ++idx) {
            const auto& object {*(objects.at(idx))};
            // sort layers by Z into buckets
            for (Layer* layer : object.layers) {
                if (layers.count(scale_(layer->print_z)) == 0) { // initialize bucket if empty
                    layers[scale_(layer->print_z)] = LayerPtrs();
                    z.emplace_back(scale_(layer->print_z));
                }
                layers[scale_(layer->print_z)].emplace_back(layer);
            }
            for (Layer* layer : object.support_layers) { // don't use auto here to not have to cast later
                if (layers.count(scale_(layer->print_z)) == 0) { // initialize bucket if empty
                    layers[scale_(layer->print_z)] = LayerPtrs();
                    z.emplace_back(scale_(layer->print_z));
                }
                layers[scale_(layer->print_z)].emplace_back(layer);
            }
        }

        // pass the comparator to leave no doubt.
        std::sort(z.begin(), z.end(),  std::less<size_t>());
        
        //  call process_layers in the order given by obj_idx
        for (const auto& print_z : z) {
            for (const auto& idx : obj_idx) {
                for (const auto* layer : layers.at(print_z)) {
                    this->process_layer(idx, layer, layer->object()->_shifted_copies);
                }
            }
        }
        
        this->flush_filters();
    }

    // Write end commands to file.
    fh << gcodegen.retract(); // TODO: process this retract through PressureRegulator in order to discharge fully

    // set bed temperature
    if (config.has_heatbed && temp > 0 && std::regex_search(config.end_gcode.getString(), bed_temp_regex)) {
        fh << gcodegen.writer.set_bed_temperature(0, 0);
    }

    // Get filament stats
    print.filament_stats.clear();
    print.total_used_filament = 0.0;
    print.total_extruded_volume = 0.0;
    print.total_weight = 0.0;
    print.total_cost = 0.0;

    
    for (auto extruder_pair : gcodegen.writer.extruders) {
        const auto& extruder {extruder_pair.second};
        auto used_material {extruder.used_filament()};
        auto extruded_volume {extruder.extruded_volume()};
        auto material_weight {extruded_volume * extruder.filament_density() / 1000.0};
        auto material_cost { material_weight * (extruder.filament_cost() / 1000.0)};

        print.filament_stats[extruder.id] = used_material;

        fh << "; material used = ";
        fh << std::fixed << std::setprecision(2) << used_material << "mm ";
        fh << "(" << std::fixed << std::setprecision(2) 
           << extruded_volume / 1000.0 
           << used_material << "cm3)\n";

        if (material_weight > 0) {
            print.total_weight += material_weight;
            fh << "; material used = " 
               << std::fixed << std::setprecision(2) << material_weight << "g\n";
            if (material_cost > 0) {
                print.total_cost += material_cost;
                fh << "; material cost = " 
                   << std::fixed << std::setprecision(2) << material_weight << "g\n";
            }
        }
        print.total_used_filament += used_material;
        print.total_extruded_volume += extruded_volume;
    }
    fh << "; total filament cost = " 
       << std::fixed << std::setprecision(2) << print.total_cost << "\n";

    // Append full config
    fh << std::endl;

    // print config
    _print_config(print.config);
    _print_config(print.default_object_config);
    _print_config(print.default_region_config);
}

std::string 
PrintGCode::filter(const std::string& in, bool wait) 
{
    return in;
}

void
PrintGCode::process_layer(size_t idx, const Layer* layer, const Points& copies)
{
    std::string gcode {""};
    auto& gcodegen {this->_gcodegen};
    const auto& print {this->_print};
    const auto& config {this->config};

    const auto& obj {*(layer->object())};
    gcodegen.config.apply(obj.config, true);

    // check for usage of spiralvase logic.


    // if using spiralvase, disable loop clipping.

    // initialize autospeed.
    {
        // get the minimum cross-section used in the layer.
        std::vector<double> mm3_per_mm;
        for (auto region_id = 0U; region_id < print.regions.size(); ++region_id) {
            const auto& region {print.regions.at(region_id)};
            const auto& layerm {layer->get_region(region_id)};

            if (!(region->config.get_abs_value("perimeter_speed") > 0 &&
                region->config.get_abs_value("small_perimeter_speed") > 0 &&
                region->config.get_abs_value("external_perimeter_speed") > 0 &&
                region->config.get_abs_value("bridge_speed") > 0)) 
            {
                mm3_per_mm.emplace_back(layerm->perimeters.min_mm3_per_mm());
            }
            if (!(region->config.get_abs_value("infill_speed") > 0 &&
                region->config.get_abs_value("solid_infill_speed") > 0 &&
                region->config.get_abs_value("top_solid_infill_speed") > 0 &&
                region->config.get_abs_value("bridge_speed") > 0 &&
                region->config.get_abs_value("gap_fill_speed") > 0)) // TODO: make this configurable? 
            {
                mm3_per_mm.emplace_back(layerm->fills.min_mm3_per_mm());
            }
        }
        if (typeid(layer) == typeid(SupportLayer*)) {
            const SupportLayer* slayer = dynamic_cast<const SupportLayer*>(layer);
            if (!(obj.config.get_abs_value("support_material_speed") > 0 && 
                  obj.config.get_abs_value("support_material_interface_speed") > 0))
            {
                mm3_per_mm.emplace_back(slayer->support_fills.min_mm3_per_mm());
                mm3_per_mm.emplace_back(slayer->support_interface_fills.min_mm3_per_mm());
            }

        }

        // ignore too-thin segments.
        // TODO make the definition of "too thin" based on a config somewhere
        mm3_per_mm.erase(std::remove_if(mm3_per_mm.begin(), mm3_per_mm.end(), [] (const double& vol) { return vol <= 0.01;} ), mm3_per_mm.end());
        if (mm3_per_mm.size() > 0) {
            const auto min_mm3_per_mm {*(std::min_element(mm3_per_mm.begin(), mm3_per_mm.end()))};
            // In order to honor max_print_speed we need to find a target volumetric
            // speed that we can use throughout the print. So we define this target 
            // volumetric speed as the volumetric speed produced by printing the 
            // smallest cross-section at the maximum speed: any larger cross-section
            // will need slower feedrates.
            auto volumetric_speed {min_mm3_per_mm * config.max_print_speed};
            if (config.max_volumetric_speed > 0) {
                volumetric_speed = std::min(volumetric_speed, config.max_volumetric_speed.getFloat());
            }
            gcodegen.volumetric_speed = volumetric_speed;
        }
    }
    // set the second layer + temp
    if (!this->_second_layer_things_done && layer->id() == 1) {
        for (const auto& extruder_ref : gcodegen.writer.extruders) {
            const auto& extruder { extruder_ref.second };
            auto temp { config.temperature.get_at(extruder.id) };

            if (temp > 0 && temp != config.first_layer_temperature.get_at(extruder.id) )
                gcode += gcodegen.writer.set_temperature(temp, 0, extruder.id);

        }
        if (config.has_heatbed && print.config.first_layer_bed_temperature > 0 && print.config.bed_temperature != print.config.first_layer_bed_temperature) {
            gcode += gcodegen.writer.set_bed_temperature(print.config.bed_temperature);
        }
        this->_second_layer_things_done = true;
    }

    // set new layer - this will change Z and force a retraction if retract_layer_change is enabled
    if (print.config.before_layer_gcode.getString().size() > 0) {
        auto pp {*(gcodegen.placeholder_parser)};
        pp.set("layer_num", gcodegen.layer_index);
        pp.set("layer_z", layer->print_z);
        pp.set("current_retraction", gcodegen.writer.extruder()->retracted);

        gcode += apply_math(pp.process(print.config.layer_gcode.getString()));
        gcode += "\n";
    }

    
    // extrude skirt along raft layers and normal obj layers
    // (not along interlaced support material layers)
    if (layer->id() < static_cast<size_t>(obj.config.raft_layers)
        || ((print.has_infinite_skirt() || _skirt_done.size() == 0 || (_skirt_done.rbegin())->first < print.config.skirt_height)
        && _skirt_done.count(scale_(layer->print_z)) == 0
        && typeid(layer) != typeid(SupportLayer*)) ) {


        gcodegen.set_origin(Pointf(0,0));
        gcodegen.avoid_crossing_perimeters.use_external_mp = true;
        
        /// data load 
        std::vector<size_t> extruder_ids;
        extruder_ids.reserve(gcodegen.writer.extruders.size());
        std::transform(gcodegen.writer.extruders.cbegin(), gcodegen.writer.extruders.cend(), std::back_inserter(extruder_ids), 
                       [] (const std::pair<unsigned int, Extruder>& z) -> std::size_t { return z.second.id; } );
        gcode += gcodegen.set_extruder(extruder_ids.at(0));

        // skip skirt if a large brim
        if (print.has_infinite_skirt() || layer->id() < static_cast<size_t>(print.config.skirt_height)) {
            const auto& skirt_flow {print.skirt_flow()};

            // distribute skirt loops across all extruders in layer 0
            auto skirt_loops {print.skirt.flatten().entities};
            for (size_t i = 0; i < skirt_loops.size(); ++i) {
                
                // when printing layers > 0 ignore 'min_skirt_length' and 
                // just use the 'skirts' setting; also just use the current extruder
                if (layer->id() > 0 && i >= static_cast<size_t>(print.config.skirts)) break; 
                const auto extruder_id { extruder_ids.at((i / extruder_ids.size()) % extruder_ids.size()) };
                if (layer->id() == 0)
                    gcode += gcodegen.set_extruder(extruder_id);

                // adjust flow according to layer height
                auto& loop {*(dynamic_cast<ExtrusionLoop*>(skirt_loops.at(i)))};
                {
                    Flow layer_skirt_flow(skirt_flow);
                    layer_skirt_flow.height = layer->height;

                    auto mm3_per_mm {layer_skirt_flow.mm3_per_mm()};

                    for (auto& path : loop.paths) {
                        path.height = layer->height;
                        path.mm3_per_mm = mm3_per_mm;
                    }
                }
                gcode += gcodegen.extrude(loop, "skirt", obj.config.support_material_speed);
            }

        }

        this->_skirt_done[scale_(layer->print_z)] = true; 
        gcodegen.avoid_crossing_perimeters.use_external_mp = false;

        if (layer->id() == 0) gcodegen.avoid_crossing_perimeters.disable_once = true;
    }

    // extrude brim
    if (this->_brim_done) {
        gcode += gcodegen.set_extruder(print.brim_extruder() - 1);
        gcodegen.set_origin(Pointf(0,0));
        gcodegen.avoid_crossing_perimeters.use_external_mp = true;
        for (const auto& b : print.brim.entities) {
            gcode += gcodegen.extrude(*b, "brim", obj.config.get_abs_value("support_material_speed"));
        }
        this->_brim_done = true;
        gcodegen.avoid_crossing_perimeters.use_external_mp = false;
        
        // allow a straight travel move to the first object point
        gcodegen.avoid_crossing_perimeters.disable_once = true;
    }

    auto copy_idx = 0U;
    for (const auto& copy : copies) {
        if (config.label_printed_objects) {
            gcode +=   "; printing object " + obj.model_object().name + " id:" + std::to_string(idx) + " copy "  + std::to_string(copy_idx) + "\n"; 
        }

        // when starting a new object, use the external motion planner for the first travel move
        if (this->_last_obj_copy.first != copy && this->_last_obj_copy.second )
            gcodegen.avoid_crossing_perimeters.use_external_mp = true;
        this->_last_obj_copy.first = copy;
        this->_last_obj_copy.second = true;
        gcodegen.set_origin(Pointf::new_unscale(copy));

        // extrude support material before other things because it might use a lower Z
        // and also because we avoid travelling on other things when printing it
        if(layer->is_support()) {
            const SupportLayer* slayer = dynamic_cast<const SupportLayer*>(layer);
            ExtrusionEntityCollection paths; 
            if (slayer->support_interface_fills.size() > 0) {
                gcode += gcodegen.set_extruder(obj.config.support_material_interface_extruder - 1);
                slayer->support_interface_fills.chained_path_from(gcodegen.last_pos(), &paths, false);
                for (const auto& path : paths) {
                    gcode += gcodegen.extrude(*path, "support material interface", obj.config.get_abs_value("support_material_interface_speed"));
                }
            }
            if (slayer->support_fills.size() > 0) {
                gcode += gcodegen.set_extruder(obj.config.support_material_extruder - 1);
                slayer->support_fills.chained_path_from(gcodegen.last_pos(), &paths, false);
                for (const auto& path : paths) {
                    gcode += gcodegen.extrude(*path, "support material", obj.config.get_abs_value("support_material_speed"));
                }
            }
        }
        // We now define a strategy for building perimeters and fills. The separation 
        // between regions doesn't matter in terms of printing order, as we follow 
        // another logic instead:
        // - we group all extrusions by extruder so that we minimize toolchanges
        // - we start from the last used extruder
        // - for each extruder, we group extrusions by island
        // - for each island, we extrude perimeters first, unless user set the infill_first
        //   option
        // (Still, we have to keep track of regions because we need to apply their config)

        // group extrusions by extruder and then by island
        //       extruder        island
        std::map<size_t,std::map<size_t,
            //                  region
            std::tuple<std::map<size_t,ExtrusionEntityCollection>, // perimeters
                       std::map<size_t,ExtrusionEntityCollection>>  // infill
        >> by_extruder;

        // cache bounding boxes of layer slices
        std::vector<BoundingBox> layer_slices_bb;
        std::transform(layer->slices.cbegin(), layer->slices.cend(), std::back_inserter(layer_slices_bb), [] (const ExPolygon& s)-> BoundingBox { return s.bounding_box(); });
        auto point_inside_surface { [&layer_slices_bb, &layer] (size_t i, Point point) -> bool {
            const auto& bbox {layer_slices_bb.at(i)};
            return bbox.contains(point) && layer->slices.at(i).contour.contains(point);
        }};
        const auto n_slices {layer->slices.size()};

        for (auto region_id = 0U; region_id < print.regions.size(); ++region_id) {
            LayerRegion* layerm;
            try {
                layerm = const_cast<LayerRegion*>(layer->get_region(region_id));
            } catch (std::out_of_range &e) {
                continue; // if no regions, bail;
            }
            auto* region {print.get_region(region_id)};
            // process perimeters
            {
                auto extruder_id = region->config.perimeter_extruder-1;
                // Casting away const just to avoid double dereferences
                for(auto* perimeter_coll : const_cast<LayerRegion*>(layerm)->perimeters) {
                    if(perimeter_coll->length() == 0) continue;  // this shouldn't happen but first_point() would fail
                    
                    // perimeter_coll is an ExtrusionPath::Collection object representing a single slice
                    for(auto i = 0U; i < n_slices; i++){
                        if (// perimeter_coll->first_point does not fit inside any slice
                            i == n_slices - 1
                            // perimeter_coll->first_point fits inside ith slice
                            || point_inside_surface(i, perimeter_coll->first_point())) {
                            std::get<0>(by_extruder[extruder_id][i])[region_id].append(*perimeter_coll);
                            break;
                        }
                    }
                }
            }
            
            // process infill
            // $layerm->fills is a collection of ExtrusionPath::Collection objects, each one containing
            // the ExtrusionPath objects of a certain infill "group" (also called "surface"
            // throughout the code). We can redefine the order of such Collections but we have to 
            // do each one completely at once.
            for(auto* fill : const_cast<LayerRegion*>(layerm)->fills) {
                if(fill->length() == 0) continue;  // this shouldn't happen but first_point() would fail
                
                auto extruder_id = fill->is_solid_infill()
                    ? region->config.solid_infill_extruder-1
                    : region->config.infill_extruder-1;
                
                // $fill is an ExtrusionPath::Collection object
                for(auto i = 0U; i < n_slices; i++){
                    if (i == n_slices - 1
                        || point_inside_surface(i, fill->first_point())) {
                        std::get<1>(by_extruder[extruder_id][i])[region_id].append(*fill);
                        break;
                    }
                }
            }
        }
        
        // tweak extruder ordering to save toolchanges
        
        auto last_extruder = gcodegen.writer.extruder()->id;
        if (by_extruder.count(last_extruder)) {
            for(auto &island : by_extruder[last_extruder]) {
               if (print.config.infill_first()) {
                    gcode += this->_extrude_infill(std::get<1>(island.second));
                    gcode += this->_extrude_perimeters(std::get<0>(island.second));
                } else {
                    gcode += this->_extrude_perimeters(std::get<0>(island.second));
                    gcode += this->_extrude_infill(std::get<1>(island.second));
                }
            }
        }
        for(auto &pair : by_extruder) {
            if(pair.first == last_extruder)continue;
            gcode += gcodegen.set_extruder(pair.first);
            for(auto &island : pair.second) {
               if (print.config.infill_first()) {
                    gcode += this->_extrude_infill(std::get<1>(island.second));
                    gcode += this->_extrude_perimeters(std::get<0>(island.second));
                } else {
                    gcode += this->_extrude_perimeters(std::get<0>(island.second));
                    gcode += this->_extrude_infill(std::get<1>(island.second));
                }
            }
        }
        if (config.label_printed_objects) {
            gcode +=   "; stop printing object " + obj.model_object().name + " id:" + std::to_string(idx) + " copy "  + std::to_string(copy_idx) + "\n";
        }
        copy_idx++;
    }
    // write the resulting gcode
    fh << this->filter(gcode);
}


// Extrude perimeters: Decide where to put seams (hide or align seams).
std::string
PrintGCode::_extrude_perimeters(std::map<size_t,ExtrusionEntityCollection> &by_region)
{
    std::string gcode = "";
    for(auto& pair : by_region) {
        this->_gcodegen.config.apply(this->_print.get_region(pair.first)->config);
        for(auto& ee : pair.second){
            gcode += this->_gcodegen.extrude(*ee, "perimeter");
        }
    }
    return gcode;
}

// Chain the paths hierarchically by a greedy algorithm to minimize a travel distance.
std::string
PrintGCode::_extrude_infill(std::map<size_t,ExtrusionEntityCollection> &by_region)
{
    std::string gcode = "";
    for(auto& pair : by_region) {
        this->_gcodegen.config.apply(this->_print.get_region(pair.first)->config);
        ExtrusionEntityCollection tmp;
        pair.second.chained_path_from(this->_gcodegen.last_pos(),&tmp);
        for(auto& ee : tmp){
            gcode += this->_gcodegen.extrude(*ee, "infill");
        }
    }
    return gcode;
}


void
PrintGCode::_print_first_layer_temperature(bool wait) 
{
    auto& gcodegen {this->_gcodegen};
    auto& fh {this->fh};
    const auto& print {this->_print};
    const auto& config {this->config};
    const auto extruders {print.extruders()};

    for (auto& t : extruders) {
        auto temp { config.first_layer_temperature.get_at(t) };
        if (config.ooze_prevention.value) temp += config.standby_temperature_delta.value;
        if (temp > 0) fh << gcodegen.writer.set_temperature(temp, wait, t);
    }
}

void 
PrintGCode::_print_config(const ConfigBase& config)
{
    for (const auto& key : config.keys()) {
        // skip if a shortcut option
        //            if (std::find(print_config_def.cbegin(), print_config_def.cend(), key) > 0) continue;
        fh << "; " << key << " = " << config.serialize(key) << "\n";
    }
}

PrintGCode::PrintGCode(Slic3r::Print& print, std::ostream& _fh) : 
        _print(print), 
        config(print.config), 
        _gcodegen(Slic3r::GCode()),
        objects(print.objects),
        fh(_fh),
        _cooling_buffer(Slic3r::CoolingBuffer(this->_gcodegen)),
        _spiral_vase(Slic3r::SpiralVase(this->config))
{ 
    size_t layer_count {0};
    if (config.complete_objects) {
        layer_count = std::accumulate(objects.cbegin(), objects.cend(), layer_count, [](const size_t& ret, const PrintObject* obj){ return ret + (obj->copies().size() * obj->total_layer_count()); });
    } else {
        layer_count = std::accumulate(objects.cbegin(), objects.cend(), layer_count, [](const size_t& ret, const PrintObject* obj){ return ret + obj->total_layer_count(); });
    }
    _gcodegen.placeholder_parser = &(print.placeholder_parser); // initialize 
    _gcodegen.layer_count = layer_count;
    _gcodegen.enable_cooling_markers = true;
    _gcodegen.apply_print_config(config);

    auto extruders {print.extruders()}; 
    _gcodegen.set_extruders(extruders.cbegin(), extruders.cend());
}

} // namespace Slic3r
#endif //SLIC3RXS
