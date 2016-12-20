---
layout: default
---

<ul id="articles">
{% for post in site.posts %}
  <li>
    <article class="article article-short">
      <header>
        <h2><a href="{{ post.url | prepend: site.github.url }}">{{ post.title }}</a></h2>
        <time datetime="{{ post.date | data_to_xmlschema }}">{{ post.date | date: "%B %-d, %Y" }}</time>
      </header>

      {{ post.excerpt }}
      
      <a href="{{ post.url }}">Continue reading</a>
    </article>
  </li>
{% endfor %}
</ul>
