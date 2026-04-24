<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('help_page.header') }}
          </h3>
        </div>

        <div class="px-4 py-5">

          <i18n-t keypath="help_page.contact_text" tag="p">
            <template v-slot:lf_on_reddit>
              <a href="https://www.reddit.com/user/lordfili" class="text-blue-600 hover:text-blue-800">lordfili</a>
            </template>
            <template v-slot:thorrak_on_hbt>
              <a href="https://www.homebrewtalk.com/forum/members/thorrak.221138/" class="text-blue-600 hover:text-blue-800">Thorrak</a>
            </template>
            <template v-slot:github>
              <a href="https://github.com/thorrak/tiltbridge" class="text-blue-600 hover:text-blue-800">GitHub</a>
            </template>
            <template v-slot:blog_post>
              <a href="https://blog.fermentrack.com/how-to-get-ahold-of-me/" class="text-blue-600 hover:text-blue-800">{{ $t('help_page.blog_post') }}</a>
            </template>
          </i18n-t>


          <p class="pt-3">
            {{ $t('help_page.tiltbridge_docs_intro') }}
          </p>

          <ul class="list-inside list-disc pt-3 px-3">
            <!-- TiltBridge Homepage Link -->
            <i18n-t keypath="help_page.tiltbridge_link" tag="li">
              <template v-slot:tb_homepage_link>
                <a href="https://www.tiltbridge.com/" class="text-blue-600 hover:text-blue-800">{{ $t('help_page.tiltbridge_homepage') }}</a>
              </template>
            </i18n-t>

            <!-- TiltBridge Docs Link -->
            <i18n-t keypath="help_page.documentation_link" tag="li">
              <template v-slot:tb_docs_link>
                <a href="http://docs.tiltbridge.com/" class="text-blue-600 hover:text-blue-800">{{ $t('help_page.documentation') }}</a>
              </template>
            </i18n-t>

            <!-- TiltBridge GitHub Link -->
            <i18n-t keypath="help_page.github_link" tag="li">
              <template v-slot:github_link>
                <a href="https://github.com/thorrak/tiltbridge/" class="text-blue-600 hover:text-blue-800">GitHub</a>
              </template>
            </i18n-t>

            <!-- TiltBridge GitHub Issues Link -->
            <i18n-t keypath="help_page.report_issue_link" tag="li">
              <template v-slot:issue_link>
                <a href="https://github.com/thorrak/tiltbridge/issues" class="text-blue-600 hover:text-blue-800">{{ $t('help_page.issue_bug_feature_request') }}</a>
              </template>
            </i18n-t>

            <!-- TiltBridge HBT Thread Link -->
            <i18n-t keypath="help_page.homebrewtalk_link" tag="li">
              <template v-slot:hbt_link>
                <a href="http://homebrewtalk.tiltbridge.com/" class="text-blue-600 hover:text-blue-800">{{ $t('help_page.homebrewtalk_thread') }}</a>
              </template>
            </i18n-t>
          </ul>
        </div>

<!--        &lt;!&ndash; Card Footer &ndash;&gt;-->
<!--        <div class="bg-white px-4 py-5 border-t border-gray-200 sm:px-6 sm:flex sm:flex-row-reverse">-->
<!--          <a href="/settings/" class="inline-flex justify-center rounded-md border border-transparent shadow-sm px-4 py-2 bg-blue-600 text-base font-medium text-white hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500 sm:w-auto sm:text-sm">Go to Settings</a>-->
<!--        </div>-->

      </div>
    </div>
  </div>
</template>

<script setup>

</script>

<style scoped>

</style>
