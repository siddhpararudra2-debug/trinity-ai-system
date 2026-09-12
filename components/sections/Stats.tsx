import AnimatedCounter from '@/components/ui/AnimatedCounter';

const stats = [
  { end: 25, suffix: '+', label: 'Years of Excellence' },
  { end: 500, suffix: '+', label: 'Projects Delivered' },
  { end: 150, suffix: '+', label: 'Expert Engineers' },
  { end: 12, suffix: '', label: 'Countries Served' },
];

export default function Stats() {
  return (
    <section className="stats" id="stats">
      <div className="container">
        <div className="stats__grid">
          {stats.map((stat) => (
            <AnimatedCounter
              key={stat.label}
              end={stat.end}
              suffix={stat.suffix}
              label={stat.label}
            />
          ))}
        </div>
      </div>
    </section>
  );
}
