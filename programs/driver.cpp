/*****************************************************************************
 * The "driver" program described in the README file.
 *****************************************************************************/

#ifdef GOOGLE_PROFILER
#include <gperftools/profiler.h>
#endif
#include <errno.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <iostream>
#include <cmath>
#include <fstream>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <Ravelin/Log.h>
#include <Moby/XMLTree.h>
#include <Moby/XMLReader.h>
#include <Moby/XMLWriter.h>
#include <Moby/SDFReader.h>

#ifdef USE_OSG
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osg/Geode>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgGA/TrackballManipulator>
#include <osgGA/StateSetManipulator>
#endif

#include <Moby/Log.h>
#include <Moby/Simulator.h>
#include <Moby/RigidBody.h>
#include <Moby/TimeSteppingSimulator.h>

using boost::shared_ptr;
using Ravelin::Vector3d;
using Ravelin::Vector2d;

namespace Moby{
  /// Handles for dynamic library loading
  std::vector<void*> handles;
  
  /// Horizontal and vertical resolutions for offscreen-rendering
  const unsigned HORZ_RES = 1024;
  const unsigned VERT_RES = 768;
  
  /// Beginning iteration for logging
  unsigned LOG_START = 0;
  
  /// Ending iteration for logging
  unsigned LOG_STOP = std::numeric_limits<unsigned>::max();
  
  /// The logging reporting level
  unsigned LOG_REPORTING_LEVEL = 0;
  
  /// The default simulation step size
  const double DEFAULT_STEP_SIZE = .001;
  
  /// The simulation step size (set to negative initially as a flag)
  double STEP_SIZE = -1.0;
  
  /// The time of the first simulation step
  double FIRST_STEP_TIME = -1;
  
  /// The time of the last simulation step
  double LAST_STEP_TIME = 0;
  
  /// The current simulation iteration
  unsigned ITER = 1;
  
  /// Interval for offscreen renders (0=offscreen renders active for first and last iterations)
  int IMAGE_IVAL = -1;
  
  /// Interval for 3D outputs (0=3D outputs active for first and last iterations)
  int THREED_IVAL = -1;
  
  /// Interval for pickling
  int PICKLE_IVAL = -1;
  
  /// Determines whether to do onscreen rendering (false by default)
  bool ONSCREEN_RENDER = false;
  
  /// Last pickle iteration
  int LAST_PICKLE = -1;
  
  /// Extension/format for 3D outputs (default=Wavefront obj)
  char THREED_EXT[5] = "obj";
  
  /// Determines whether to update graphics (false by default, but certain
  /// options will set to true)
  bool UPDATE_GRAPHICS = false;
  
  /// The maximum number of iterations (default infinity)
  unsigned MAX_ITER = std::numeric_limits<unsigned>::max();
  
  /// The maximum time of the simulation (default infinity)
  double MAX_TIME = std::numeric_limits<double>::max();
  
  /// The total (CPU) clock time used by the simulation
  double TOTAL_TIME = 0.0;
  
  /// Last 3D output iteration and time output
  int LAST_3D_WRITTEN = -1;
  
  /// Last image iteration output
  unsigned LAST_IMG_WRITTEN = -1;
  
  /// Outputs to stdout
  bool OUTPUT_FRAME_RATE = false;
  bool OUTPUT_ITER_NUM = false;
  bool OUTPUT_SIM_RATE = false;
  
  /// Render Contact Points
  bool RENDER_CONTACT_POINTS = false;
  
  /// The map of objects read from the simulation XML file
  std::map<std::string, BasePtr> READ_MAP;
  
#ifdef USE_OSG
  /// The OpenInventor group node for Moby
  osg::Group* MOBY_GROUP;
  
  /// The OpenInventor root group node for this application
  osg::Group* MAIN_GROUP;
  
  /// Pointer to the viewer
  osgViewer::Viewer* viewer_pointer;
#endif
  
  /// Pointer to the controller's initializer, called once (if any)
  typedef void (*init_t)(void*, const std::map<std::string, BasePtr>&, double);
  std::vector<init_t> INIT;
  
  /// Checks whether was compiled with OpenSceneGraph support
  bool check_osg()
  {
#ifdef USE_OSG
    return true;
#else
    return false;
#endif
  }
  
  /// Gets the current time (as a floating-point number)
  double get_current_time()
  {
    const double MICROSEC = 1.0/1000000;
    timeval t;
    gettimeofday(&t, NULL);
    return (double) t.tv_sec + (double) t.tv_usec * MICROSEC;
  }
  
  /// runs the simulator and updates all transforms
  bool step(boost::shared_ptr<Simulator> s)
  {
#ifdef USE_OSG
    if (ONSCREEN_RENDER)
    {
      if (viewer_pointer->done())
        return false;
      viewer_pointer->frame();
    }
#endif
    
    // get the simulator as event driven simulation
    boost::shared_ptr<TimeSteppingSimulator> eds = boost::dynamic_pointer_cast<TimeSteppingSimulator>( s );
    
    // see whether to activate logging
    if (ITER >= LOG_START && ITER <= LOG_STOP)
    {
      Moby::Log<OutputToFile>::reporting_level = LOG_REPORTING_LEVEL;
      Ravelin::Log<OutputToFile>::reporting_level = LOG_REPORTING_LEVEL;
    }
    else
    {
      Moby::Log<OutputToFile>::reporting_level = 0;
      Ravelin::Log<OutputToFile>::reporting_level = 0;
    }
    
    // output the iteration #
    if (OUTPUT_ITER_NUM)
      std::cout << "iteration: " << ITER << "  simulation time: " << s->current_time << std::endl;
    if (Moby::Log<OutputToFile>::reporting_level > 0)
      FILE_LOG(Moby::Log<OutputToFile>::reporting_level) << "iteration: " << ITER << "  simulation time: " << s->current_time << std::endl;
    
    // only update the graphics if it is necessary; update visualization first
    // in case simulator takes some time to perform first step
    if (UPDATE_GRAPHICS)
      s->update_visualization();
    
    // output the image, if desired
#ifdef USE_OSG
    if (IMAGE_IVAL > 0)
    {
      // determine at what iteration nearest frame would be output
      if (ITER % IMAGE_IVAL == 0)
      {
        char buffer[128];
        sprintf(buffer, "driver.out.%08u.png", ++LAST_IMG_WRITTEN);
        // TODO: call offscreen renderer
      }
    }
    
    // output the 3D file, if desired
    if (THREED_IVAL > 0)
    {
      // determine at what iteration nearest frame would be output
      if (ITER % THREED_IVAL == 0)
      {
        // write the file (fails silently)
        char buffer[128];
        sprintf(buffer, "driver.out-%08u-%f.%s", ++LAST_3D_WRITTEN, s->current_time, THREED_EXT);
        osgDB::writeNodeFile(*MAIN_GROUP, std::string(buffer));
      }
    }
#endif
    
    // serialize the simulation, if desired
    if (PICKLE_IVAL > 0)
    {
      // determine at what iteration nearest pickle would be output
      if (ITER % PICKLE_IVAL == 0)
      {
        // write the file (fails silently)
        char buffer[128];
        sprintf(buffer, "driver.out-%08u-%f.xml", ++LAST_PICKLE, s->current_time);
        XMLWriter::serialize_to_xml(std::string(buffer), s);
      }
    }
    
    // step the simulator
    if(OUTPUT_SIM_RATE){
      // output the iteration / stepping rate
      clock_t pre_sim_t = clock();
      s->step(STEP_SIZE);
      clock_t post_sim_t = clock();
      double total_t = (post_sim_t - pre_sim_t) / (double) CLOCKS_PER_SEC;
      TOTAL_TIME += total_t;
      std::cout << "time to compute last iteration: " << total_t << " (" << TOTAL_TIME / ITER << "s/iter, " << TOTAL_TIME / s->current_time << "s/step)" << std::endl;
    } else {
      s->step(STEP_SIZE);
    }
    
    
    // output the frame rate, if desired
    if (OUTPUT_FRAME_RATE)
    {
      double tm = get_current_time();
      std::cout << "instantaneous frame rate: " << (1.0/(tm - LAST_STEP_TIME)) << "fps  avg. frame rate: " << (ITER / (tm - FIRST_STEP_TIME)) << "fps" << std::endl;
      LAST_STEP_TIME = tm;
    }
    
    // if render contact points enabled, notify the Simulator
    if( RENDER_CONTACT_POINTS && eds)
      eds->render_contact_points = true;
    
    // Output first and last frames
    if (ITER >= MAX_ITER || s->current_time > MAX_TIME || ITER == 1) {
      // only update the graphics if it is necessary; update visualization first
      // in case simulator takes some time to perform first step
      if (UPDATE_GRAPHICS)
        s->update_visualization();

      // output the image, if desired
#ifdef USE_OSG
      if (IMAGE_IVAL > 0)
      {
        char buffer[128];
        sprintf(buffer, "driver.out.%08u.png", ++LAST_IMG_WRITTEN);
        // TODO: call offscreen renderer
      }
      
      // output the 3D file, if desired
      if (THREED_IVAL > 0)
      {
        // write the file (fails silently)
        char buffer[128];
        sprintf(buffer, "driver.out-%08u-%f.%s", ++LAST_3D_WRITTEN, s->current_time, THREED_EXT);
        osgDB::writeNodeFile(*MAIN_GROUP, std::string(buffer));
      }
#endif
      
      // serialize the simulation, if desired
      if (PICKLE_IVAL > 0)
      {
        // write the file (fails silently)
        char buffer[128];
        sprintf(buffer, "driver.out-%08u-%f.xml", ++LAST_PICKLE, s->current_time);
        XMLWriter::serialize_to_xml(std::string(buffer), s);
      }
    }
    
    // check that maximum number of iterations or maximum time not exceeded
    if (ITER >= MAX_ITER || s->current_time > MAX_TIME){
      return false;
    }
    
    // update the iteration #
    ITER++;
    
    return true;
  }
  
  // attempts to read control code plugin
  void read_plugin(const char* filename)
  {
    // attempt to read the file
    void* plugin = dlopen(filename, RTLD_LAZY);
    if (!plugin)
    {
      // get the error string, in case we need it
      char* dlerror_str = dlerror();
      
      // attempt to use the plugin path
      char* plugin_path = getenv("MOBY_PLUGIN_PATH");
      if (plugin_path)
      {
        // get the plugin path and make sure it has a path string at the end
        std::string plugin_path_str(plugin_path);
        if (plugin_path_str.at(plugin_path_str.size()-1) != '/')
          plugin_path_str += '/';
        
        // concatenate
        plugin_path_str += filename;
        
        // attempt to re-open the plugin
        plugin = dlopen(plugin_path_str.c_str(), RTLD_LAZY);
      }
      
      // check whether the plugin was successfully loaded
      if (!plugin)
      {
        std::cerr << "driver: failed to read plugin from " << filename << " (or using " << plugin_path << ")" << std::endl;
        std::cerr << "  " << dlerror_str << std::endl;
        exit(-1);
      }
    }
    handles.push_back(plugin);
    
    // attempt to load the initializer
    dlerror();
    INIT.push_back((init_t) dlsym(plugin, "init"));
    const char* dlsym_error = dlerror();
    if (dlsym_error)
    {
      std::cerr << "driver warning: cannot load symbol 'init' from " << filename << std::endl;
      std::cerr << "        error follows: " << std::endl << dlsym_error << std::endl;
      exit(-1);
    }
  }
  
  /// Adds lights to the scene when no scene background file specified
  void add_lights()
  {
#ifdef USE_OSG
    // add lights
#endif
  }
  
  /// Gets the XML sub-tree rooted at the specified tag
  shared_ptr<const XMLTree> find_subtree(shared_ptr<const XMLTree> root, const std::string& name)
  {
    // if we found the tree, return it
    if (strcasecmp(root->name.c_str(), name.c_str()) == 0)
      return root;
    
    // otherwise, look for it recursively
    const std::list<XMLTreePtr>& children = root->children;
    for (std::list<XMLTreePtr>::const_iterator i = children.begin(); i != children.end(); i++)
    {
      shared_ptr<const XMLTree> node = find_subtree(*i, name);
      if (node)
        return node;
    }
    
    // return NULL if we are here
    return shared_ptr<const XMLTree>();
  }
  
  // finds and processes given XML tags
  void process_tag(const std::string& tag, shared_ptr<const XMLTree> root, void (*fn)(shared_ptr<const XMLTree>))
  {
    // if this node is of the given type, process it
    if (strcasecmp(root->name.c_str(), tag.c_str()) == 0)
      fn(root);
    else
    {
      const std::list<XMLTreePtr>& child_nodes = root->children;
      for (std::list<XMLTreePtr>::const_iterator i = child_nodes.begin(); i != child_nodes.end(); i++)
        process_tag(tag, *i, fn);
    }
  }
  
  /// processes the 'camera' tag
  void process_camera_tag(shared_ptr<const XMLTree> node)
  {
    if (!ONSCREEN_RENDER)
      return;
    
    // read all attributes
    XMLAttrib* target_attr = node->get_attrib("target");
    XMLAttrib* position_attr = node->get_attrib("position");
    XMLAttrib* up_attr = node->get_attrib("up");
    if (!target_attr || !position_attr || !up_attr)
      return;
    
    // get the actual values
    Vector3d up;
    Point3d target, position;
    target_attr->get_vector_value(target);
    position_attr->get_vector_value(position);
    up_attr->get_vector_value(up);
    
    // setup osg vectors
#ifdef USE_OSG
    osg::Vec3d position_osg(position[0], position[1], position[2]);
    osg::Vec3d target_osg(target[0], target[1], target[2]);
    osg::Vec3d up_osg(up[0], up[1], up[2]);
    
    // set the camera view
    if (viewer_pointer && viewer_pointer->getCameraManipulator())
    {
      osg::Camera* camera = viewer_pointer->getCamera();
      camera->setViewMatrixAsLookAt(position_osg, target_osg, up_osg);
      
      // setup the manipulator using the camera, if necessary
      viewer_pointer->getCameraManipulator()->setHomePosition(position_osg, target_osg, up_osg);
    }
#endif
  }
  
  /// processes the 'window' tag
  void process_window_tag(shared_ptr<const XMLTree> node)
  {
    // don't process if not onscreen rendering
    if (!ONSCREEN_RENDER)
      return;
    
#ifdef USE_OSG
    // read window location
    XMLAttrib* loc_attr = node->get_attrib("location");
    
    // read window size
    XMLAttrib* size_attr = node->get_attrib("size");
    
    // get the actual values
    Vector2d loc(0,0), size(640,480);
    if (loc_attr)
      loc_attr->get_vector_value(loc);
    if (size_attr)
      size_attr->get_vector_value(size);
    
    // setup the window
    viewer_pointer->setUpViewInWindow(loc[0], loc[1], size[0], size[1]);
#endif
  }
  
  /// processes all 'driver' options in the XML file
  void process_xml_options(const std::string& xml_fname)
  {
    // *************************************************************
    // going to remove any path from the argument and change to that
    // path; this is done so that all files referenced from the
    // local path of the XML file are found
    // *************************************************************
    
    // set the filename to use as the argument, by default
    std::string filename = xml_fname;
    
    // get the current pathname
    size_t BUFSIZE = 128;
    boost::shared_array<char> cwd;
    while (true)
    {
      cwd = boost::shared_array<char>((new char[BUFSIZE]));
      if (getcwd(cwd.get(), BUFSIZE) == cwd.get())
        break;
      if (errno != ERANGE)
      {
        std::cerr << "process_xml_options() - unable to allocate sufficient memory!" << std::endl;
        return;
      }
      BUFSIZE *= 2;
    }
    
    // separate the path from the filename
    size_t last_path_sep = xml_fname.find_last_of('/');
    if (last_path_sep != std::string::npos)
    {
      // get the new working path
      std::string pathname = xml_fname.substr(0,last_path_sep+1);
      
      // change to the new working path
      chdir(pathname.c_str());
      
      // get the new filename
      filename = xml_fname.substr(last_path_sep+1,std::string::npos);
    }
    
    // read the XML Tree
    shared_ptr<const XMLTree> driver_tree = XMLTree::read_from_xml(filename);
    if (!driver_tree)
    {
      std::cerr << "process_xml_options() - unable to open file " << xml_fname;
      std::cerr << " for reading" << std::endl;
      chdir(cwd.get());
      return;
    }
    
    // find the driver tree G
    driver_tree = find_subtree(driver_tree, "driver");
    
    // make sure that the driver node was found
    if (!driver_tree)
    {
      chdir(cwd.get());
      return;
    }

    // look for the step size attribute; only set it if it's not set already
    XMLAttrib* step_size_attr = driver_tree->get_attrib("step-size");
    if (step_size_attr && STEP_SIZE < 0.0)
      STEP_SIZE = step_size_attr->get_real_value();
    
    // process tags
    process_tag("window", driver_tree, process_window_tag);
    process_tag("camera", driver_tree, process_camera_tag);
    
    // change back to current directory
    chdir(cwd.get());
  }
  
  // where everything begins...
  
  int init(int argc, char** argv, boost::shared_ptr<Simulator>& s)
  {
    // get the simulator pointer
    const unsigned ONECHAR_ARG = 3, TWOCHAR_ARG = 4;
    
#ifdef GOOGLE_PROFILER
    ProfilerStart("/tmp/profile");
#endif
    
#ifdef USE_OSG
    const double DYNAMICS_FREQ = 0.001;
    viewer_pointer = new osgViewer::Viewer();
    viewer_pointer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
#endif
    
    // setup some default options
    std::string scene_path;
    THREED_IVAL = 0;
    IMAGE_IVAL = 0;
    
    // check that syntax is ok
    if (argc < 2)
    {
      std::cerr << "syntax: driver [OPTIONS] <xml file>" << std::endl;
      std::cerr << "        (see README for OPTIONS)" << std::endl;
      return -1;
    }
    
    // get all options
    for (int i=1; i< argc-1; i++)
    {
      // get the option
      std::string option(argv[i]);
      
      // process options
      if (option == "-r")
      {
        ONSCREEN_RENDER = true;
        UPDATE_GRAPHICS = true;
        check_osg();
      }
      else if (option.find("-of") != std::string::npos)
        OUTPUT_FRAME_RATE = true;
      else if (option.find("-oi") != std::string::npos)
        OUTPUT_ITER_NUM = true;
      else if (option.find("-or") != std::string::npos)
        OUTPUT_SIM_RATE = true;
      else if (option.find("-w=") != std::string::npos)
      {
        PICKLE_IVAL = std::atoi(&argv[i][ONECHAR_ARG]);
        assert(PICKLE_IVAL >= 0);
      }
      else if (option.find("-v=") != std::string::npos)
      {
        UPDATE_GRAPHICS = true;
        check_osg();
        THREED_IVAL = std::atoi(&argv[i][ONECHAR_ARG]);
        assert(THREED_IVAL >= 0);
      }
      else if (option.find("-i=") != std::string::npos)
      {
        check_osg();
        UPDATE_GRAPHICS = true;
        IMAGE_IVAL = std::atoi(&argv[i][ONECHAR_ARG]);
        assert(IMAGE_IVAL >= 0);
      }
      else if (option.find("-s=") != std::string::npos)
      {
        STEP_SIZE = std::atof(&argv[i][ONECHAR_ARG]);
      }
      else if (option.find("-lf=") != std::string::npos)
      {
        std::string fname(&argv[i][TWOCHAR_ARG]);
        Moby::OutputToFile::stream.open(fname.c_str());
        Ravelin::OutputToFile::stream.open(fname.c_str());
      }
      else if (option.find("-l=") != std::string::npos)
      {
        LOG_REPORTING_LEVEL = std::atoi(&argv[i][ONECHAR_ARG]);
        Moby::Log<OutputToFile>::reporting_level = LOG_REPORTING_LEVEL;
        Ravelin::Log<OutputToFile>::reporting_level = LOG_REPORTING_LEVEL;
      }
      else if (option.find("-lt=") != std::string::npos)
      {
        LOG_START = std::atoi(&argv[i][TWOCHAR_ARG]);
      }
      else if (option.find("-lp=") != std::string::npos)
      {
        LOG_STOP = std::atoi(&argv[i][TWOCHAR_ARG]);
      }
      else if (option.find("-mi=") != std::string::npos)
      {
        MAX_ITER = std::atoi(&argv[i][TWOCHAR_ARG]);
        assert(MAX_ITER > 0);
      }
      else if (option.find("-mt=") != std::string::npos)
      {
        MAX_TIME = std::atof(&argv[i][TWOCHAR_ARG]);
        assert(MAX_TIME >= 0.0);
      }
      else if (option.find("-x=") != std::string::npos)
      {
        check_osg();
        scene_path = std::string(&argv[i][ONECHAR_ARG]);
      }
      else if (option.find("-p=") != std::string::npos)
      {
        std::vector<std::string> plugins;
        std::string arg = std::string(&argv[i][ONECHAR_ARG]);
        boost::split(plugins, arg, boost::is_any_of(","));
        for(size_t i = 0; i < plugins.size(); ++i){
          read_plugin(plugins[i].c_str());
        }
      }
      else if (option.find("-y=") != std::string::npos)
      {
        strcpy(THREED_EXT, &argv[i][ONECHAR_ARG]);
      } else if (option.find("-vcp") != std::string::npos)
        RENDER_CONTACT_POINTS = true;
      
    }
    
    // setup the simulation
    if (std::string(argv[argc-1]).find(".xml") != std::string::npos)
      READ_MAP = XMLReader::read(std::string(argv[argc-1]));
    else if (std::string(argv[argc-1]).find(".sdf") != std::string::npos)
    {
      // artificially create the read map
      shared_ptr<TimeSteppingSimulator> eds = SDFReader::read(std::string(argv[argc-1]));
      READ_MAP[eds->id] = eds;
      BOOST_FOREACH(ControlledBodyPtr db, eds->get_dynamic_bodies())
      READ_MAP[db->id] = db;
    }
    
    // setup the offscreen renderer if necessary
#ifdef USE_OSG
    if (IMAGE_IVAL > 0)
    {
      // TODO: setup offscreen renderer here
    }
#endif
    // get the (only) simulation object
    for (std::map<std::string, BasePtr>::const_iterator i = READ_MAP.begin(); i != READ_MAP.end(); i++)
    {
      s = boost::dynamic_pointer_cast<Simulator>(i->second);
      if (s)
        break;
    }
    
    // make sure that a simulator was found
    if (!s)
    {
      std::cerr << "driver: no simulator found in " << argv[argc-1] << std::endl;
      return -1;
    }
    
    // setup osg window if desired
#ifdef USE_OSG
    if (ONSCREEN_RENDER)
    {
      // setup any necessary handlers here
      viewer_pointer->setCameraManipulator(new osgGA::TrackballManipulator());
      viewer_pointer->addEventHandler(new osgGA::StateSetManipulator(viewer_pointer->getCamera()->getOrCreateStateSet()));
      viewer_pointer->addEventHandler(new osgViewer::WindowSizeHandler);
      viewer_pointer->addEventHandler(new osgViewer::StatsHandler);
    }
    
    // init the main group
    MAIN_GROUP = new osg::Group;
#endif
    
    // call the initializers, if any
    if (!INIT.empty())
    {
#ifdef USE_OSG
      BOOST_FOREACH(init_t i, INIT)
      (*i)(MAIN_GROUP, READ_MAP, STEP_SIZE);
#else
      BOOST_FOREACH(init_t i, INIT)
      (*i)(NULL, READ_MAP, STEP_SIZE);
#endif
    }
    
    // look for a scene description file
#ifdef USE_OSG
    if (scene_path != "")
    {
      std::ifstream in(scene_path.c_str());
      if (in.fail())
      {
        std::cerr << "driver: unable to find scene description from " << scene_path << std::endl;
        add_lights();
      }
      else
      {
        in.close();
        osg::Node* node = osgDB::readNodeFile(scene_path);
        if (!node)
        {
          std::cerr << "driver: unable to open scene description file!" << std::endl;
          add_lights();
        }
        else
          MAIN_GROUP->addChild(node);
      }
    }
    else
      add_lights();
#endif
    
    // process XML options (if possible)
    if (std::string(argv[argc-1]).find(".xml") != std::string::npos)
      process_xml_options(std::string(argv[argc-1]));
    
    // get the simulator visualization
#ifdef USE_OSG
    MAIN_GROUP->addChild(s->get_persistent_vdata());
    MAIN_GROUP->addChild(s->get_transient_vdata());
#endif
    
    // setup the timers
    FIRST_STEP_TIME = get_current_time();
    LAST_STEP_TIME = FIRST_STEP_TIME;
    
    // prepare to render
#ifdef USE_OSG
    if (ONSCREEN_RENDER)
    {
      viewer_pointer->setSceneData(MAIN_GROUP);
      viewer_pointer->realize();
    }
#endif
    
    return 0;
  }
  
  int close(){
    // close the loaded library
    for(size_t i = 0; i < handles.size(); ++i){
      dlclose(handles[i]);
    }
   
#ifdef USE_OSG 
    delete viewer_pointer;
#endif    

#ifdef GOOGLE_PROFILER
    ProfilerStop();
#endif
    
    return 0;
  }
  
  int init(int argc, char** argv){
    boost::shared_ptr<Simulator> s;
    
    init(argc,argv,s);

    // if the step size is negative, set the step size to a default
    if (STEP_SIZE < 0.0)
      STEP_SIZE = DEFAULT_STEP_SIZE;

    // begin simulating 
    bool stop_sim = false;
    while (!stop_sim)
    {
      stop_sim = !step(s);
    }
    
    close();
    
    return 0;
  }
} // end namespace Moby
