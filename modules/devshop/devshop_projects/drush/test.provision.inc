<?php

/**
 * Pre provision-devshop-test
 */
function drush_devshop_provision_pre_provision_test(){
  $tests = drush_get_option('tests-to-run');

  if (!empty($tests) && is_string($tests)) {
    drush_set_option('tests-to-run', array_filter(explode(',', $tests)));
  }
  
  if (!empty($tests) && is_array($tests)) {
    drush_set_option('tests-to-run', array_filter($tests));
  }
}

/**
 * Implements the provision-devshop-test command.
 */
function drush_devshop_provision_provision_test() {

  $project_name = d()->project;
  $project = (object) d("@project_{$project_name}")->project;
  $environment = (object) $project->environments[d()->environment];

  drush_log(dt('Provision DevShop Run Tests started...'), 'status');

  // Get tests to run
  if (!empty($project->settings['testing']['test_type'])) {
    $type = $project->settings['testing']['test_type'];
  }
  else {
    $type = drush_get_option('test-type', NULL);
  }

  if ($project->settings['testing']['tests_to_run'] === NULL) {
    $tests = array();
  }
  else {
    $tests = array_filter($project->settings['testing']['tests_to_run']);
  }
  $tests_to_run = drush_get_option('tests-to-run', $tests);

  // Run Simpletest
  if ($type == 'simpletest') {
    drush_log(dt("Running $type tests $tests_to_run"), 'ok');
    provision_backend_invoke('@self', 'en simpletest');
    provision_backend_invoke('@self', 'test-run', array($tests_to_run));
  }
  elseif ($type == 'behat') {
    // Get paths from options or site context.
    $repo_path = d()->platform->repo_path? d()->platform->repo_path: d()->platform->root;
    $behat_folder_path = drush_get_option('behat-folder-path', $project->settings['testing']['behat_folder_path']);

    // If no repo at that path, error.
    if (!file_exists($repo_path)){
      return drush_set_error('DEVSHOP_MISSING_FILE', dt('repo_path does not exist.'));
    }
    // If no file at behat bin path, error.
    $tests_folder = $repo_path  . '/' .$behat_folder_path;
    $yml_path = $repo_path  . '/' .$behat_folder_path . '/behat.yml';
    if (!file_exists($yml_path)){

      $message = <<<TXT
Your project is not yet ready to run Behat tests. Please follow the following steps:

  1. Create a folder called '!path' in your repo, or create your own folder 
     and reconfigure your Project Settings > Testing.
  2. Copy the files from https://github.com/opendevshop/devmaster/tree/1.x/modules/devshop/devshop_testing/tests_example
  3. Write more tests!
  
TXT;


      drush_log(dt($message, array('!path' => $behat_folder_path)), 'error');
      return drush_set_error('DEVSHOP_MISSING_FILE', dt('Your project is not yet ready to run Behat tests. Please follow the instructions and try again.'));
    }

    // Prepare path and command
    $full_behat_folder_path = $repo_path . '/' . $behat_folder_path;
    $full_behat_bin_path = $repo_path . '/' . $behat_folder_path . '/bin/behat';

    // Load the behat.yml from behat_folder_path.
    if (file_exists($full_behat_folder_path . '/behat.yml')) {
      $behat_yml = file_get_contents($full_behat_folder_path . '/behat.yml');
    }
    elseif (file_exists($full_behat_folder_path . '/config/behat.yml')) {
      $behat_yml = file_get_contents($full_behat_folder_path . '/config/behat.yml');
    }
    else {
      return drush_set_error('DEVSHOP_MISSING_FILE', dt('behat.yml file not found in behat_folder_path: ') . $behat_folder_path);
    }

    // Run composer install.
    devshop_process('composer install', $full_behat_folder_path, 'DevShop Testing');

    // Write custom behat.yml to temporary folder.
    $environment_name = $environment->name;
    $alias = d()->name;
    $root = d()->root;

    $username = d()->http_basic_auth_username;
    $password = d()->http_basic_auth_password;
    $uri = d()->uri;
    $url = d()->ssl_enabled?
      "https://$uri":
      "http://$uri";
    
    if (!empty($username)) {
      $url = d()->ssl_enabled?
        "https://$username:$password@$uri":
        "http://$username:$password@$uri";
    }

    $behat_yml = <<<YML
# This file is autogenerated by DevShop Testing every time a test run is triggered.
#
# This is done so we can dynamically set path, drush alias, and URL.
# To run this manually, you can run any of the following commands:
# 
# bin/behat features/ --config behat.ENVIRONMENTNAME.yml --profile ENVIRONMENTNAME --colors --format-settings='{"expand": true}'
#  - or - 
# drush @$url provision-test
 
# The following is just a copy of the behat.yml file found in this folder:

$behat_yml

# DevShop Environment Profile
# This profile is generated based on the current environment. 
# Base URL, Alias, and Drupal are all dynamic.
 
devshop:
  extensions:
    Behat\MinkExtension:
      base_url: '$url'
    Drupal\DrupalExtension:
      drush:
        alias: '$alias'
      drupal:
        drupal_root: '$root'

YML;

    $behat_yml_file = $full_behat_folder_path . "/behat.devshop.yml";
    file_put_contents($behat_yml_file, $behat_yml);

    drush_log(dt('Wrote custom behat.yml configuration file to @file', array(
      '@file' => $behat_yml_file,
    )), 'ok');

    // Run behat tests for each feature.
    $no_errors = TRUE;
    $test_result = '';
    
    // Fill an empty item if empty so we run all the tests.
    if (empty($tests_to_run)) {
      $tests_to_run[] = '';
    }
    
    // Foreach test to run...
    $i = 0;
    foreach ($tests_to_run as $feature) {
      $i++;

      // Check for path.
      if (substr($feature, 0, 1) !== '/') {
        $feature = '/'.$feature;
      }
      $feature_path = empty($feature) ? '' : "features{$feature}";

      // Create Command
      // The extra profile is used so we can dynamically set the URL and drush alias of the behat.yml.
      // the "colors" option is to force it to output ANSI colors. "format-settings" "expand: true" is so
      // Behat will output all of the steps when using "Scenario Outlines".
      $cmd = "$full_behat_bin_path $feature_path --config $behat_yml_file --profile devshop --colors --strict --format-settings='{\"expand\": true}'";

      // Run behat command
      devshop_process($cmd, $full_behat_folder_path, 'DevShop Testing', array(), TRUE, 'Test Run Failed');
    }
  }
}

